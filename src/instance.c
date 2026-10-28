#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "boundary.h"
#include "calls.h"
#include "descriptors.h"
#include "objects.h"
#include "platform.h"
#include "trap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wasm_export.h>

/* The room for WAMR's message when instantiating fails, which Stasis reads no further. */
enum { WAMR_MESSAGE_SIZE = 128 };

/* The instance that's running on this thread, if any. Calls into scripts never nest, so while it
 * runs, no instance can be started or called on the thread, itself included. */
static _Thread_local stasis_instance *running;

/* Frees an instance's own memory, once WAMR's part of it is destroyed. */
static void free_instance(stasis_instance *instance) {
    stasis_trap_record_free(&instance->trap);
    stasis_deallocate(instance->scratch);
    stasis_deallocate(instance->results);
    stasis_deallocate((void *)instance->entry_points);
    stasis_deallocate((void *)instance->post_returns);
    stasis_deallocate(instance);
}

stasis_status stasis_instance_create(stasis_script *script, void *user_data,
                                     stasis_instance **instance) {
    if (instance == NULL) {
        return STASIS_INVALID_ARGUMENT;
    }
    *instance = NULL;
    if (script == NULL) {
        return STASIS_INVALID_ARGUMENT;
    }
    stasis_instance *created = stasis_allocate_zeroed(1, sizeof *created);
    if (created == NULL) {
        return STASIS_OUT_OF_MEMORY;
    }
    created->entry_points = (wasm_function_inst_t *)stasis_allocate_zeroed(
        script->entry_point_count, sizeof *created->entry_points);
    created->post_returns = (wasm_function_inst_t *)stasis_allocate_zeroed(
        script->entry_point_count, sizeof *created->post_returns);
    if (created->entry_points == NULL || created->post_returns == NULL) {
        free_instance(created);
        return STASIS_OUT_OF_MEMORY;
    }
    if (script->host_functions.scratch_size > 0) {
        created->scratch = stasis_allocate(script->host_functions.scratch_size);
        if (created->scratch == NULL) {
            free_instance(created);
            return STASIS_OUT_OF_MEMORY;
        }
    }

    /* Each instance has its own execution environment, with the library's stack, and no heap of
     * WAMR's inside its memory. Every failure to instantiate is taken for a lack of memory, which
     * is all it can be, since loading turns away every module that WAMR can't instantiate for
     * another reason, or would run code in while instantiating: one that imports anything but
     * functions, has a start section or exports a function a backend runs by itself, has a segment
     * that doesn't fit, or has a memory that starts larger than an instance may have. */
    const uint32_t stack_size = script->library->stack_size;
    InstantiationArgs args;
    memset(&args, 0, sizeof args);
    args.default_stack_size = stack_size;
    args.host_managed_heap_size = 0;
    args.max_memory_pages = STASIS_MAX_MEMORY_PAGES;
    char wamr_message[WAMR_MESSAGE_SIZE];
    stasis_lock();
    created->module_instance =
        wasm_runtime_instantiate_ex(script->module, &args, wamr_message, sizeof wamr_message);
    if (created->module_instance != NULL) {
        created->exec_env = wasm_runtime_create_exec_env(created->module_instance, stack_size);
        if (created->exec_env == NULL) {
            wasm_runtime_deinstantiate(created->module_instance);
        }
    }
    if (created->exec_env != NULL) {
        script->instance_count++;
    }
    stasis_unlock();
    if (created->exec_env == NULL) {
        free_instance(created);
        return STASIS_OUT_OF_MEMORY;
    }

    /* Host functions find the instance through its execution environment. */
    wasm_runtime_set_user_data(created->exec_env, created);
    created->script = script;
    created->user_data = user_data;
    created->state = INSTANCE_CREATED;
    if (script->initializes) {
        created->initialize =
            wasm_runtime_lookup_function(created->module_instance, STASIS_INITIALIZE_NAME);
    }
    created->reallocate =
        wasm_runtime_lookup_function(created->module_instance, STASIS_REALLOC_NAME);
    for (size_t id = 0; id < script->entry_point_count; id++) {
        if (script->entry_points[id] != NULL) {
            created->entry_points[id] =
                wasm_runtime_lookup_function(created->module_instance, script->entry_points[id]);
        }
        if (script->post_returns[id] != NULL) {
            created->post_returns[id] =
                wasm_runtime_lookup_function(created->module_instance, script->post_returns[id]);
        }
    }
    *instance = created;
    return STASIS_OK;
}

/* Starts a call into a ready instance. */
static void enter(stasis_instance *instance) {
    instance->state = INSTANCE_RUNNING;
    running = instance;
}

/* Ends a call into an instance that returned, or that trapped, which leaves the instance trapped,
 * with its trap report. */
static stasis_status leave(stasis_instance *instance, bool returned) {
    running = NULL;
    if (!returned) {
        stasis_keep_trap(instance);
    }
    stasis_raised_trap_clear(&instance->raised);
    instance->state = returned ? INSTANCE_READY : INSTANCE_TRAPPED;
    return returned ? STASIS_OK : STASIS_TRAP;
}

stasis_status stasis_instance_start(stasis_instance *instance) {
    if (instance == NULL) {
        return STASIS_INVALID_ARGUMENT;
    }
    if (running != NULL) {
        return STASIS_REFUSED_NESTED;
    }
    switch (instance->state) {
    case INSTANCE_CREATED:
        break;
    case INSTANCE_READY:
        return STASIS_REFUSED_STARTED;
    case INSTANCE_RUNNING:
        return STASIS_REFUSED_NESTED;
    case INSTANCE_TRAPPED:
        return STASIS_REFUSED_TRAPPED;
    }
    if (instance->initialize == NULL) {
        instance->state = INSTANCE_READY;
        return STASIS_OK;
    }
    enter(instance);
    return leave(instance, stasis_invoke(instance, instance->initialize, 0, NULL, 0, NULL));
}

/* Whether the host's arguments to an entry point can cross: each is a value that can cross, and
 * every string can, and they flatten into no more core values than an entry point can take. Past
 * that, they'd spill into the script's memory, which they can't yet. */
static bool arguments_cross(const stasis_entry_point *descriptor, const void *arguments) {
    size_t flat_count = 0;
    for (size_t i = 0; i < descriptor->parameter_count; i++) {
        const stasis_value *parameter = &descriptor->parameters[i];
        if (!stasis_can_cross(parameter)) {
            return false;
        }
        if (parameter->type == STASIS_TYPE_STRING) {
            stasis_string string;
            memcpy(&string, (const unsigned char *)arguments + parameter->offset, sizeof string);
            size_t valid = 0;
            if (stasis_check_host_string(string, &valid) != STRING_CROSSES) {
                return false;
            }
        }
        flat_count += stasis_flat_count(parameter);
    }
    return flat_count <= STASIS_MAX_FLAT_PARAMETERS;
}

/* Lowers the host's arguments to an entry point into the core values at `core`, putting each
 * string into the running instance's memory, and sets `count` to the number of core values. Returns
 * false when the call is to end, as stasis_place_string does. */
static bool lower_arguments(stasis_instance *instance, const stasis_entry_point *descriptor,
                            const void *arguments, wasm_val_t *core, uint32_t *count) {
    size_t slot = 0;
    for (size_t i = 0; i < descriptor->parameter_count; i++) {
        const stasis_value *parameter = &descriptor->parameters[i];
        if (parameter->type != STASIS_TYPE_STRING) {
            stasis_lower_scalar(parameter, arguments, &core[slot++]);
            continue;
        }
        stasis_string string;
        memcpy(&string, (const unsigned char *)arguments + parameter->offset, sizeof string);
        const struct placed_value value = {descriptor, i + 1, NULL};
        uint32_t address = 0;
        if (!stasis_place_string(instance, &value, string, &address)) {
            return false;
        }
        /* A string flattens into its address and size, each a u32. */
        const uint32_t size = (uint32_t)string.size;
        const stasis_value u32 = {STASIS_TYPE_U32, 0, 0};
        stasis_lower_scalar(&u32, &address, &core[slot++]);
        stasis_lower_scalar(&u32, &size, &core[slot++]);
    }
    *count = (uint32_t)slot;
    return true;
}

/* Makes room for `size` bytes in the instance's buffer for results, which grows to at least twice
 * its size when it has to, so that after the first few calls, it no longer has to. Returns false if
 * there's no memory for it. */
static bool reserve_results(stasis_instance *instance, size_t size) {
    if (size <= instance->results_size) {
        return true;
    }
    const size_t doubled =
        instance->results_size > SIZE_MAX / 2 ? SIZE_MAX : instance->results_size * 2;
    const size_t grown = size > doubled ? size : doubled;
    unsigned char *results = stasis_allocate(grown);
    if (results == NULL) {
        return false;
    }
    stasis_deallocate(instance->results);
    instance->results = results;
    instance->results_size = grown;
    return true;
}

/* How a string result fared as Stasis lifted it. */
enum lifted {
    RESULT_LIFTED,
    RESULT_LIFTED_BUT_NO_MEMORY,
    RESULT_NOT_LIFTED,
};

/* Lifts the string that an entry point returned in the return area at `area` in the script's
 * memory, copying its bytes into the instance's buffer for results, into the stasis_string at
 * `result`. Returns RESULT_NOT_LIFTED, having raised a boundary violation, if the area or the
 * string isn't all in memory, the area is misaligned, or the string isn't UTF-8, and
 * RESULT_LIFTED_BUT_NO_MEMORY,
 * leaving the result alone, if there's no memory for the buffer. */
static enum lifted lift_string_result(stasis_instance *instance,
                                      const stasis_entry_point *descriptor, uint32_t area,
                                      void *result) {
    const char *kind = instance->script->interface_data->scripts[0].name;
    const struct script_memory memory = stasis_script_memory(instance->module_instance);
    const stasis_value string_type = {STASIS_TYPE_STRING, 0, 0};
    const size_t alignment = stasis_memory_alignment(&string_type);
    const size_t area_size = stasis_memory_size(&string_type);
    if (area % alignment != 0) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the entry point `%s.%s` returned its result at %lu in the "
                                "script's memory, which isn't a multiple of its alignment, %lu",
                                kind, descriptor->name, (unsigned long)area,
                                (unsigned long)alignment);
        return RESULT_NOT_LIFTED;
    }
    if ((uint64_t)area + area_size > memory.size) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the entry point `%s.%s` returned its result at %lu in the "
                                "script's memory, where its %lu bytes run past its end at %llu",
                                kind, descriptor->name, (unsigned long)area,
                                (unsigned long)area_size, (unsigned long long)memory.size);
        return RESULT_NOT_LIFTED;
    }
    const uint32_t address = stasis_load_u32(memory.base + area);
    const uint32_t size = stasis_load_u32(memory.base + area + 4);
    if ((uint64_t)address + size > memory.size) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the entry point `%s.%s` returned a string at %lu in the script's "
                                "memory, where its %lu bytes run past its end at %llu",
                                kind, descriptor->name, (unsigned long)address, (unsigned long)size,
                                (unsigned long long)memory.size);
        return RESULT_NOT_LIFTED;
    }
    const size_t valid = stasis_utf8_length(memory.base + address, size);
    if (valid < size) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the entry point `%s.%s` returned a string of %lu bytes that isn't "
                                "UTF-8, from offset %lu",
                                kind, descriptor->name, (unsigned long)size, (unsigned long)valid);
        return RESULT_NOT_LIFTED;
    }
    if (!reserve_results(instance, size)) {
        return RESULT_LIFTED_BUT_NO_MEMORY;
    }
    stasis_string string = {NULL, size};
    if (size > 0) {
        memcpy(instance->results, memory.base + address, size);
        string.data = (const char *)instance->results;
    }
    memcpy(result, &string, sizeof string);
    return RESULT_LIFTED;
}

/* Calls the entry point that `descriptor` describes and `id` names, as stasis_call does, leaving
 * the result alone unless the call succeeds. */
static stasis_status call(stasis_instance *instance, const stasis_interface *interface_data,
                          const stasis_entry_point *descriptor, uint32_t id, const void *arguments,
                          void *result) {
    if (instance == NULL || interface_data != instance->script->interface_data) {
        return STASIS_INVALID_ARGUMENT;
    }
    if (descriptor->parameter_count > 0 && arguments == NULL) {
        return STASIS_INVALID_ARGUMENT;
    }
    if (descriptor->result != NULL && result == NULL) {
        return STASIS_INVALID_ARGUMENT;
    }
    if (!arguments_cross(descriptor, arguments)) {
        return STASIS_INVALID_ARGUMENT;
    }
    if (descriptor->result != NULL && !stasis_can_cross(descriptor->result)) {
        return STASIS_INVALID_ARGUMENT;
    }

    if (running != NULL) {
        return STASIS_REFUSED_NESTED;
    }
    switch (instance->state) {
    case INSTANCE_READY:
        break;
    case INSTANCE_CREATED:
        return STASIS_REFUSED_NOT_STARTED;
    case INSTANCE_RUNNING:
        return STASIS_REFUSED_NESTED;
    case INSTANCE_TRAPPED:
        return STASIS_REFUSED_TRAPPED;
    }
    wasm_function_inst_t function = instance->entry_points[id];
    if (function == NULL) {
        return STASIS_REFUSED_ABSENT;
    }

    enter(instance);
    wasm_val_t core_arguments[STASIS_MAX_FLAT_PARAMETERS];
    uint32_t argument_count = 0;
    bool returned =
        lower_arguments(instance, descriptor, arguments, core_arguments, &argument_count);
    wasm_val_t core_result;
    memset(&core_result, 0, sizeof core_result);
    const uint32_t result_count = descriptor->result != NULL ? 1 : 0;
    if (returned) {
        returned = stasis_invoke(instance, function, result_count, &core_result, argument_count,
                                 core_arguments);
    }
    enum lifted lifted = RESULT_LIFTED;
    if (returned && descriptor->result != NULL) {
        if (descriptor->result->type == STASIS_TYPE_STRING) {
            /* A string flattens into two core values, more than a function can return, so the
             * script returns the address of a return area that holds them. */
            uint32_t area = 0;
            memcpy(&area, &core_result.of.i32, sizeof area);
            lifted = lift_string_result(instance, descriptor, area, result);
            returned = lifted != RESULT_NOT_LIFTED;
        } else {
            stasis_lift_scalar(descriptor->result, &core_result, result);
        }
    }
    /* The script frees its results even when there's no memory to copy them, since they'd stay
     * in its memory otherwise, and in every snapshot of it. */
    wasm_function_inst_t post_return = instance->post_returns[id];
    if (returned && post_return != NULL) {
        returned =
            stasis_post_return(instance, descriptor, post_return, result_count, &core_result);
    }
    const stasis_status status = leave(instance, returned);
    return status == STASIS_OK && lifted == RESULT_LIFTED_BUT_NO_MEMORY ? STASIS_OUT_OF_MEMORY
                                                                        : status;
}

stasis_status stasis_call(stasis_instance *instance, const stasis_interface *interface_data,
                          uint32_t entry_point, const void *arguments, void *result) {
    const stasis_script_kind *kind =
        interface_data == NULL ? NULL : stasis_interface_script_kind(interface_data);
    if (kind == NULL || entry_point >= kind->entry_point_count) {
        return STASIS_INVALID_ARGUMENT;
    }
    const stasis_entry_point *descriptor = &kind->entry_points[entry_point];
    const stasis_status status =
        call(instance, interface_data, descriptor, entry_point, arguments, result);
    if (status != STASIS_OK && descriptor->result != NULL && result != NULL) {
        memset(result, 0, stasis_value_size(interface_data, descriptor->result));
    }
    return status;
}

stasis_status stasis_instance_destroy(stasis_instance *instance) {
    if (instance == NULL) {
        return STASIS_OK;
    }
    if (instance->state == INSTANCE_RUNNING) {
        return STASIS_REFUSED_RUNNING;
    }
    stasis_lock();
    wasm_runtime_destroy_exec_env(instance->exec_env);
    wasm_runtime_deinstantiate(instance->module_instance);
    instance->script->instance_count--;
    stasis_unlock();
    free_instance(instance);
    return STASIS_OK;
}

void *stasis_instance_user_data(const stasis_instance *instance) {
    return instance == NULL ? NULL : instance->user_data;
}

stasis_status stasis_abort(stasis_instance *instance, const char *message) {
    if (instance == NULL || message == NULL) {
        return STASIS_INVALID_ARGUMENT;
    }
    if (instance != running) {
        return STASIS_REFUSED_NOT_RUNNING;
    }
    if (!stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_HOST_ABORT, "%s", message)) {
        return STASIS_OUT_OF_MEMORY;
    }
    return STASIS_OK;
}

const stasis_trap *stasis_instance_trap(const stasis_instance *instance) {
    if (instance == NULL || instance->state != INSTANCE_TRAPPED) {
        return NULL;
    }
    return &instance->trap.report;
}
