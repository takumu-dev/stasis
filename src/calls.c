#include "calls.h"

#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "boundary.h"
#include "descriptors.h"
/* NOLINTNEXTLINE(misc-include-cleaner): it defines the struct that stasis.h's type names */
#include "objects.h"
#include "trap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wasm_export.h>

bool stasis_invoke(stasis_instance *instance, wasm_function_inst_t function, uint32_t result_count,
                   wasm_val_t *results, uint32_t argument_count, wasm_val_t *arguments) {
    const bool returned = wasm_runtime_call_wasm_a(instance->exec_env, function, result_count,
                                                   results, argument_count, arguments);
    if (!returned) {
        stasis_keep_trap(instance);
    }
    return returned;
}

void stasis_keep_trap(stasis_instance *instance) {
    if (!instance->trap.filled) {
        stasis_trap_record_init(&instance->trap, instance->module_instance, &instance->script->code,
                                &instance->raised);
    }
}

/* Writes how messages name `value`, as in "argument 1 of `npc.greet`" or "the result of
 * `world.name`", into the `size` bytes at `buffer`, as snprintf does. */
static int print_description(char *buffer, size_t size, const stasis_interface *interface_data,
                             const struct placed_value *value) {
    if (value->entry_point != NULL) {
        return snprintf(buffer, size, "argument %lu of `%s.%s`", (unsigned long)value->position,
                        interface_data->scripts[0].name, value->entry_point->name);
    }
    const struct host_function_name name =
        stasis_host_function_name(interface_data, value->host_function);
    return snprintf(buffer, size, "the result of " HOST_FUNCTION_NAME_FORMAT,
                    HOST_FUNCTION_NAME_ARGUMENTS(name));
}

/* Writes how messages name `value` into a block of its own, which stasis_deallocate frees. Returns
 * null if there's no memory for it. */
static char *describe(const stasis_instance *instance, const struct placed_value *value) {
    const stasis_interface *interface_data = instance->script->interface_data;
    const int length = print_description(NULL, 0, interface_data, value);
    if (length < 0) {
        return NULL;
    }
    char *description = stasis_allocate((size_t)length + 1);
    if (description != NULL) {
        (void)print_description(description, (size_t)length + 1, interface_data, value);
    }
    return description;
}

/* How a message names a value it lacks the memory to describe. */
static const char undescribed[] = "a value";

bool stasis_reallocate(stasis_instance *instance, const struct placed_value *value,
                       uint32_t alignment, uint32_t size, uint32_t *address) {
    /* No old allocation, as the canonical ABI calls it for a new value. */
    const uint32_t values[4] = {0, 0, alignment, size};
    const stasis_value u32 = {STASIS_TYPE_U32, 0, 0};
    wasm_val_t arguments[4];
    for (size_t i = 0; i < 4; i++) {
        stasis_lower_scalar(&u32, &values[i], &arguments[i]);
    }
    wasm_val_t result;
    memset(&result, 0, sizeof result);
    instance->inside = INSIDE_REALLOC;
    const bool returned = stasis_invoke(instance, instance->reallocate, 1, &result, 4, arguments);
    instance->inside = INSIDE_NEITHER;
    if (!returned) {
        return false;
    }

    uint32_t allocated = 0;
    stasis_lift_scalar(&u32, &result, &allocated);
    /* No bytes need no memory, so null is an answer for them, as the canonical ABI has it. */
    const struct script_memory memory = stasis_script_memory(instance->module_instance);
    if ((allocated != 0 || size == 0) && allocated % alignment == 0 &&
        (uint64_t)allocated + size <= memory.size) {
        *address = allocated;
        return true;
    }
    char *described = describe(instance, value);
    const char *what = described != NULL ? described : undescribed;
    if (allocated == 0) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_ALLOCATION_FAILED,
                                "the script's `" STASIS_REALLOC_NAME
                                "` returned null for the %lu bytes of %s",
                                (unsigned long)size, what);
    } else if (allocated % alignment != 0) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script's `" STASIS_REALLOC_NAME
                                "` returned %lu for the %lu bytes of %s, which isn't a multiple of "
                                "their alignment, %lu",
                                (unsigned long)allocated, (unsigned long)size, what,
                                (unsigned long)alignment);
    } else {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script's `" STASIS_REALLOC_NAME
                                "` returned %lu for the %lu bytes of %s, where they run past the "
                                "end of its memory at %llu",
                                (unsigned long)allocated, (unsigned long)size, what,
                                (unsigned long long)memory.size);
    }
    stasis_deallocate(described);
    return false;
}

bool stasis_place_string(stasis_instance *instance, const struct placed_value *value,
                         stasis_string string, uint32_t *address) {
    if (!stasis_reallocate(instance, value, 1, (uint32_t)string.size, address)) {
        return false;
    }
    /* cabi_realloc may have grown the memory, and moved it. */
    if (string.size > 0) {
        const struct script_memory memory = stasis_script_memory(instance->module_instance);
        memcpy(memory.base + *address, string.data, string.size);
    }
    return true;
}

bool stasis_post_return(stasis_instance *instance, const stasis_entry_point *entry_point,
                        wasm_function_inst_t function, uint32_t result_count, wasm_val_t *results) {
    instance->inside = INSIDE_POST_RETURN;
    instance->post_returning = entry_point;
    const bool returned = stasis_invoke(instance, function, 0, NULL, result_count, results);
    instance->inside = INSIDE_NEITHER;
    instance->post_returning = NULL;
    return returned;
}

bool stasis_may_leave(const stasis_instance *instance) {
    return instance->inside == INSIDE_NEITHER;
}

void stasis_refuse_leaving(stasis_instance *instance, const struct host_function_name *import) {
    switch (instance->inside) {
    case INSIDE_NEITHER:
        break;
    case INSIDE_REALLOC:
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script called " HOST_FUNCTION_NAME_FORMAT
                                " from its `" STASIS_REALLOC_NAME "`, where it may call no import",
                                HOST_FUNCTION_NAME_ARGUMENTS(*import));
        break;
    case INSIDE_POST_RETURN:
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script called " HOST_FUNCTION_NAME_FORMAT
                                " from its `cabi_post_*` for `%s.%s`, where it may call no import",
                                HOST_FUNCTION_NAME_ARGUMENTS(*import),
                                instance->script->interface_data->scripts[0].name,
                                instance->post_returning->name);
        break;
    }
}
