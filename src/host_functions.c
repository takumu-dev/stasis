#include "host_functions.h"

#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "boundary.h"
#include "calls.h"
#include "descriptors.h"
#include "handles.h"
#include "layout.h"
/* NOLINTNEXTLINE(misc-include-cleaner): it defines the struct that stasis.h's type names */
#include "objects.h"
#include "signature.h"
#include "trap.h"
#include "wamr_hooks.h"

#include <lib_export.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wasm_export.h>

/* The room for a line as text, such as "0.4294967295", and its NUL. */
enum { LINE_SIZE = 24 };

/* The C types that values cross in, whose strictest alignment a result's storage needs. */
union crossing {
    uint64_t integer;
    double real;
    void *pointer;
    size_t size;
};

/* Rounds `size` up to a multiple of `alignment`. */
static size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) / alignment * alignment;
}

/* Lifts the string of `size` bytes at `address` in the script's memory that is `leaf`, one of a
 * host function's arguments or in one, into the stasis_string at the leaf's offset in `arguments`,
 * pointing into the memory. Returns false, having raised a boundary violation, if it isn't all in
 * the memory, or isn't UTF-8. */
static bool lift_string(stasis_instance *instance, const stasis_host_function *function,
                        const struct leaf *leaf, uint32_t address, uint32_t size,
                        unsigned char *arguments) {
    const struct script_memory memory = stasis_script_memory(instance->module_instance);
    if ((uint64_t)address + size > memory.size) {
        const struct host_function_name name =
            stasis_host_function_name(instance->script->interface_data, function);
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed " FIELD_FORMAT
                                "argument %lu of " HOST_FUNCTION_NAME_FORMAT
                                " at %lu in its memory, where its %lu bytes run past its end at "
                                "%llu",
                                FIELD_ARGUMENTS(leaf->path), (unsigned long)leaf->position,
                                HOST_FUNCTION_NAME_ARGUMENTS(name), (unsigned long)address,
                                (unsigned long)size, (unsigned long long)memory.size);
        return false;
    }
    const size_t valid = stasis_utf8_length(memory.base + address, size);
    if (valid < size) {
        const struct host_function_name name =
            stasis_host_function_name(instance->script->interface_data, function);
        (void)stasis_raise_trap(
            &instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
            "the script passed " FIELD_FORMAT "argument %lu of " HOST_FUNCTION_NAME_FORMAT
            " as a string of %lu bytes that isn't UTF-8, from offset %lu",
            FIELD_ARGUMENTS(leaf->path), (unsigned long)leaf->position,
            HOST_FUNCTION_NAME_ARGUMENTS(name), (unsigned long)size, (unsigned long)valid);
        return false;
    }
    stasis_string string;
    string.data = (const char *)memory.base + address;
    string.size = size;
    memcpy(arguments + leaf->value.offset, &string, sizeof string);
    return true;
}

/* Lifts the handle `value` that is `leaf`, one of a host function's arguments or in one, into the
 * pointer to the host object it names, at the leaf's offset in `arguments`, which is null when the
 * handle was revoked. Returns false, having raised a boundary violation, if the instance was never
 * given it, or it names a host object of another kind. */
static bool lift_handle(stasis_instance *instance, const stasis_host_function *function,
                        const struct leaf *leaf, uint32_t value, unsigned char *arguments) {
    void *object = NULL;
    uint32_t other_kind = 0;
    const enum handle_fault fault =
        stasis_handle_lift(&instance->handles, value, leaf->value.index, &object, &other_kind);
    if (fault == HANDLE_CROSSES) {
        stasis_store_object(arguments + leaf->value.offset, object);
        return true;
    }
    const stasis_interface *interface_data = instance->script->interface_data;
    const struct host_function_name name = stasis_host_function_name(interface_data, function);
    if (fault == HANDLE_FORGED) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed " FIELD_FORMAT
                                "argument %lu of " HOST_FUNCTION_NAME_FORMAT
                                " as handle %lu, which the instance was never given",
                                FIELD_ARGUMENTS(leaf->path), (unsigned long)leaf->position,
                                HOST_FUNCTION_NAME_ARGUMENTS(name), (unsigned long)value);
    } else {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed " FIELD_FORMAT
                                "argument %lu of " HOST_FUNCTION_NAME_FORMAT
                                " as handle %lu, which names a host object of another kind, `%s`",
                                FIELD_ARGUMENTS(leaf->path), (unsigned long)leaf->position,
                                HOST_FUNCTION_NAME_ARGUMENTS(name), (unsigned long)value,
                                interface_data->kinds[other_kind].qualified_name);
    }
    return false;
}

/* Lifts a host function's arguments from the script's core arguments, each in a slot of its own,
 * into `arguments`. Returns false, having raised a boundary violation, if one can't cross. */
static bool lift_flat(stasis_instance *instance, const struct linked_function *linked,
                      const uint64_t *core, unsigned char *arguments) {
    const struct layout *parameters = &linked->layout.parameters;
    for (size_t i = 0; i < parameters->leaf_count; i++) {
        const struct leaf *leaf = &parameters->leaves[i];
        bool lifted = true;
        switch (leaf->value.type) {
        case STASIS_TYPE_STRING:
            lifted = lift_string(instance, linked->descriptor, leaf, (uint32_t)core[leaf->slot],
                                 (uint32_t)core[leaf->slot + 1], arguments);
            break;
        case STASIS_TYPE_HANDLE:
            lifted = lift_handle(instance, linked->descriptor, leaf, (uint32_t)core[leaf->slot],
                                 arguments);
            break;
        default:
            stasis_lift_raw_scalar(&leaf->value, &core[leaf->slot], arguments);
            break;
        }
        if (!lifted) {
            return false;
        }
    }
    return true;
}

/* Lifts a host function's arguments from where the script spilled them in its memory, at
 * `address`, laid out as the canonical ABI lays out a tuple of them, into `arguments`. Returns
 * false, having raised a boundary violation, if they aren't all in the memory, or aren't aligned
 * as the strictest of them needs, or if a string or a handle among them can't cross. */
static bool lift_spilled(stasis_instance *instance, const struct linked_function *linked,
                         uint32_t address, unsigned char *arguments) {
    const stasis_host_function *function = linked->descriptor;
    const struct layout *parameters = &linked->layout.parameters;
    const struct script_memory memory = stasis_script_memory(instance->module_instance);
    if (address % parameters->memory_alignment != 0) {
        const struct host_function_name name =
            stasis_host_function_name(instance->script->interface_data, function);
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed the arguments of " HOST_FUNCTION_NAME_FORMAT
                                " at %lu in its memory, which isn't a multiple of their "
                                "alignment, %lu",
                                HOST_FUNCTION_NAME_ARGUMENTS(name), (unsigned long)address,
                                (unsigned long)parameters->memory_alignment);
        return false;
    }
    if ((uint64_t)address + parameters->memory_size > memory.size) {
        const struct host_function_name name =
            stasis_host_function_name(instance->script->interface_data, function);
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed the arguments of " HOST_FUNCTION_NAME_FORMAT
                                " at %lu in its memory, where their %lu bytes run past its end at "
                                "%llu",
                                HOST_FUNCTION_NAME_ARGUMENTS(name), (unsigned long)address,
                                (unsigned long)parameters->memory_size,
                                (unsigned long long)memory.size);
        return false;
    }

    const unsigned char *tuple = memory.base + address;
    for (size_t i = 0; i < parameters->leaf_count; i++) {
        const struct leaf *leaf = &parameters->leaves[i];
        const unsigned char *at = tuple + leaf->memory_offset;
        bool lifted = true;
        switch (leaf->value.type) {
        case STASIS_TYPE_STRING:
            lifted = lift_string(instance, function, leaf, stasis_load_u32(at),
                                 stasis_load_u32(at + 4), arguments);
            break;
        case STASIS_TYPE_HANDLE:
            lifted = lift_handle(instance, function, leaf, stasis_load_u32(at), arguments);
            break;
        default:
            stasis_load_scalar(&leaf->value, at, arguments);
            break;
        }
        if (!lifted) {
            return false;
        }
    }
    return true;
}

/* Checks the return area at `area` in the script's memory that the script passed a host function
 * for its result. Returns false, having raised a boundary violation, if it isn't all in the memory,
 * or isn't aligned as the result needs. */
static bool check_area(stasis_instance *instance, const struct linked_function *linked,
                       uint32_t area) {
    const struct script_memory memory = stasis_script_memory(instance->module_instance);
    const size_t alignment = linked->layout.result.memory_alignment;
    const size_t size = linked->layout.result.memory_size;
    if (area % alignment == 0 && (uint64_t)area + size <= memory.size) {
        return true;
    }
    const struct host_function_name name =
        stasis_host_function_name(instance->script->interface_data, linked->descriptor);
    if (area % alignment != 0) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed the return area of " HOST_FUNCTION_NAME_FORMAT
                                " at %lu in its memory, which isn't a multiple of its alignment, "
                                "%lu",
                                HOST_FUNCTION_NAME_ARGUMENTS(name), (unsigned long)area,
                                (unsigned long)alignment);
    } else {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed the return area of " HOST_FUNCTION_NAME_FORMAT
                                " at %lu in its memory, where its %lu bytes run past its end at "
                                "%llu",
                                HOST_FUNCTION_NAME_ARGUMENTS(name), (unsigned long)area,
                                (unsigned long)size, (unsigned long long)memory.size);
    }
    return false;
}

/* Checks a string that a host function returned, which is `leaf` of its result. Returns false,
 * having raised a host abort, if its data is null though it isn't empty, if it's larger than a
 * string may be, or if it isn't UTF-8. */
static bool check_returned_string(stasis_instance *instance, const stasis_host_function *function,
                                  const struct leaf *leaf, stasis_string string) {
    size_t valid = 0;
    const enum string_fault fault = stasis_check_host_string(string, &valid);
    if (fault == STRING_CROSSES) {
        return true;
    }
    const struct host_function_name name =
        stasis_host_function_name(instance->script->interface_data, function);
    switch (fault) {
    case STRING_CROSSES:
        break;
    case STRING_DATA_NULL:
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_HOST_ABORT,
                                "the host function " HOST_FUNCTION_NAME_FORMAT
                                " returned " FIELD_FORMAT
                                "a string of %lu bytes whose data is null",
                                HOST_FUNCTION_NAME_ARGUMENTS(name),
                                RESULT_FIELD_ARGUMENTS(leaf->path), (unsigned long)string.size);
        break;
    case STRING_TOO_LARGE:
        (void)stasis_raise_trap(
            &instance->raised, STASIS_TRAP_KIND_HOST_ABORT,
            "the host function " HOST_FUNCTION_NAME_FORMAT " returned " FIELD_FORMAT
            "a string of %llu bytes, more than the %lu a string may have",
            HOST_FUNCTION_NAME_ARGUMENTS(name), RESULT_FIELD_ARGUMENTS(leaf->path),
            (unsigned long long)string.size, (unsigned long)STASIS_MAX_STRING_SIZE);
        break;
    case STRING_NOT_UTF8:
        (void)stasis_raise_trap(
            &instance->raised, STASIS_TRAP_KIND_HOST_ABORT,
            "the host function " HOST_FUNCTION_NAME_FORMAT " returned " FIELD_FORMAT
            "a string of %lu bytes that isn't UTF-8, from offset %lu",
            HOST_FUNCTION_NAME_ARGUMENTS(name), RESULT_FIELD_ARGUMENTS(leaf->path),
            (unsigned long)string.size, (unsigned long)valid);
        break;
    }
    return false;
}

/* Checks the result a host function returned, at `result`, before any of it reaches the script:
 * each string it holds, and each host object, which can't be null. Returns false, having raised a
 * host abort, if one can't cross. */
static bool check_result(stasis_instance *instance, const struct linked_function *linked,
                         const unsigned char *result) {
    const struct layout *results = &linked->layout.result;
    for (size_t i = 0; i < results->leaf_count; i++) {
        const struct leaf *leaf = &results->leaves[i];
        if (leaf->value.type == STASIS_TYPE_STRING) {
            stasis_string string;
            memcpy(&string, result + leaf->value.offset, sizeof string);
            if (!check_returned_string(instance, linked->descriptor, leaf, string)) {
                return false;
            }
        } else if (leaf->value.type == STASIS_TYPE_HANDLE &&
                   stasis_load_object(result + leaf->value.offset) == NULL) {
            const struct host_function_name name =
                stasis_host_function_name(instance->script->interface_data, linked->descriptor);
            (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_HOST_ABORT,
                                    "the host function " HOST_FUNCTION_NAME_FORMAT
                                    " returned " FIELD_FORMAT "a null host object",
                                    HOST_FUNCTION_NAME_ARGUMENTS(name),
                                    RESULT_FIELD_ARGUMENTS(leaf->path));
            return false;
        }
    }
    return true;
}

/* Makes room in the instance's handle table for each handle in a host function's result, before
 * the host function runs, so that lowering the result can't fail for lack of memory. Returns false,
 * having raised allocation_failed, if there's no memory for it. */
static bool reserve_handles(stasis_instance *instance, const struct linked_function *linked) {
    if (stasis_handle_table_reserve(&instance->handles, linked->layout.result.handle_count)) {
        return true;
    }
    const struct host_function_name name =
        stasis_host_function_name(instance->script->interface_data, linked->descriptor);
    (void)stasis_raise_trap(
        &instance->raised, STASIS_TRAP_KIND_ALLOCATION_FAILED,
        "there was no memory for the handles of the result of " HOST_FUNCTION_NAME_FORMAT,
        HOST_FUNCTION_NAME_ARGUMENTS(name));
    return false;
}

/* Lowers the result a host function returned, at `result`, which has been checked and flattens
 * into one core value, into the slot `core`. Returns false, having raised a trap, if the instance
 * has no handle left for it. */
static bool lower_flat_result(stasis_instance *instance, const struct linked_function *linked,
                              const unsigned char *result, uint64_t *core) {
    const stasis_value *value = &linked->layout.result.leaves[0].value;
    if (value->type != STASIS_TYPE_HANDLE) {
        stasis_lower_raw_scalar(value, result, core);
        return true;
    }
    void *object = stasis_load_object(result + value->offset);
    const struct placed_value owner = {NULL, 0, linked->descriptor, NULL};
    uint32_t handle = 0;
    if (!stasis_lower_handle(instance, &owner, value->index, object, &handle)) {
        return false;
    }
    const stasis_value u32 = {STASIS_TYPE_U32, 0, 0};
    stasis_lower_raw_scalar(&u32, &handle, core);
    return true;
}

/* What WAMR calls in place of every host function, in its raw calling convention: with the script's
 * core arguments, each in a slot of its own, and the slot that the result goes in first. The host
 * function as the script links it is its attachment, and the instance is its execution
 * environment's user data. */
static void trampoline(wasm_exec_env_t exec_env, uint64_t *core) {
    /* WAMR sets the attachment to null once a host function calls back into the script, as
     * lowering a string does, so it's read first. */
    const struct linked_function *linked = wasm_runtime_get_function_attachment(exec_env);
    const stasis_host_function *function = linked->descriptor;
    stasis_instance *instance = wasm_runtime_get_user_data(exec_env);
    if (!stasis_may_leave(instance)) {
        const struct host_function_name name =
            stasis_host_function_name(instance->script->interface_data, function);
        stasis_refuse_leaving(instance, &name);
        stasis_end_call(instance);
        return;
    }
    unsigned char *arguments = instance->scratch;
    unsigned char *result = arguments + instance->script->host_functions.result_offset;
    const size_t flat_count = linked->layout.parameters.flat_count;
    const bool spilled = flat_count > STASIS_MAX_FLAT_PARAMETERS;
    bool lifted = false;
    if (spilled) {
        lifted = lift_spilled(instance, linked, (uint32_t)core[0], arguments);
    } else {
        lifted = lift_flat(instance, linked, core, arguments);
    }
    /* A result that flattens into more core values than a function can return goes into a return
     * area of the script's, whose address the script passes after its arguments. */
    const bool has_area = linked->layout.result.flat_count > STASIS_MAX_FLAT_RESULTS;
    const uint32_t area = has_area ? (uint32_t)core[spilled ? 1 : flat_count] : 0;
    if (!lifted || (has_area && !check_area(instance, linked, area)) ||
        !reserve_handles(instance, linked)) {
        stasis_end_call(instance);
        return;
    }
    function->thunk(instance, function->parameter_count > 0 ? arguments : NULL,
                    function->result != NULL ? result : NULL);
    /* A host function that aborted the call has returned all the same, and its result is
     * ignored. */
    if (instance->raised.raised) {
        stasis_end_call(instance);
        return;
    }
    if (function->result == NULL) {
        return;
    }
    if (!check_result(instance, linked, result)) {
        stasis_end_call(instance);
        return;
    }
    if (!has_area) {
        if (!lower_flat_result(instance, linked, result, &core[0])) {
            stasis_end_call(instance);
        }
        return;
    }
    /* The result goes into the return area, with each string it holds put into the script's
     * memory. When cabi_realloc trapped of itself, WAMR unwinds the call already. */
    const struct placed_value owner = {NULL, 0, function, NULL};
    if (!stasis_store_values(instance, &linked->layout.result, result, area, &owner) &&
        instance->raised.raised) {
        stasis_end_call(instance);
    }
}

/* A group's symbols being made: measured first, with `symbols` null, and then written. It goes on
 * from group to group, measuring the scratch an instance needs. */
struct group_builder {
    struct layout_builder *layouts;
    NativeSymbol *symbols;
    char *names;
    struct linked_function *functions;
    size_t symbol_count;
    size_t names_size;
    /* The largest argument struct and result of the host functions measured. */
    size_t largest_arguments;
    size_t largest_result;
    /* STASIS_OK, or why writing a symbol failed. */
    stasis_status status;
};

/* Links a host function, or a method of the kind of host object called `object`, as the next
 * symbol, whose name is `<function>:<hash>`, or `<object>.<method>:<hash>`. */
static void add_symbol(struct group_builder *builder, const char *object,
                       const stasis_host_function *function) {
    const size_t object_size = object == NULL ? 0 : strlen(object) + 1;
    const size_t size = object_size + strlen(function->name) + 1 + STASIS_SIGNATURE_HASH_LENGTH + 1;
    if (builder->symbols != NULL) {
        char hash[STASIS_SIGNATURE_HASH_LENGTH + 1];
        if (!stasis_host_function_hash(builder->layouts->walk, function, hash)) {
            builder->status = STASIS_INVALID_ARGUMENT;
            return;
        }
        struct linked_function *linked = &builder->functions[builder->symbol_count];
        linked->descriptor = function;
        const stasis_status laid_out =
            stasis_function_layout_init(&linked->layout, builder->layouts, function->parameters,
                                        function->parameter_count, function->result);
        if (laid_out != STASIS_OK) {
            builder->status = laid_out;
            return;
        }
        char *name = builder->names + builder->names_size;
        if (object == NULL) {
            (void)snprintf(name, size, "%s:%s", function->name, hash);
        } else {
            (void)snprintf(name, size, "%s.%s:%s", object, function->name, hash);
        }
        NativeSymbol *symbol = &builder->symbols[builder->symbol_count];
        symbol->symbol = name;
        symbol->func_ptr = stasis_wamr_raw_pointer(trampoline);
        symbol->signature = NULL;
        symbol->attachment = linked;
    }
    builder->symbol_count++;
    builder->names_size += size;
    if (function->arguments_size > builder->largest_arguments) {
        builder->largest_arguments = function->arguments_size;
    }
    if (function->result != NULL) {
        const size_t result_size =
            stasis_value_size(builder->layouts->walk->interface_data, function->result);
        if (result_size > builder->largest_result) {
            builder->largest_result = result_size;
        }
    }
}

/* Adds the symbols of the group at `index`: its host functions, then its kinds' methods. */
static void add_group_symbols(struct group_builder *builder, size_t index) {
    const stasis_interface *interface_data = builder->layouts->walk->interface_data;
    const stasis_group *group = &interface_data->groups[index];
    for (size_t i = 0; i < group->function_count; i++) {
        add_symbol(builder, NULL, &group->functions[i]);
    }
    for (size_t k = 0; k < interface_data->kind_count; k++) {
        const stasis_object_kind *kind = &interface_data->kinds[k];
        if (kind->group != index) {
            continue;
        }
        for (size_t i = 0; i < kind->method_count; i++) {
            add_symbol(builder, kind->name, &kind->methods[i]);
        }
    }
}

/* Copies the group at `index`, whose module name ends in `line`, into `group`. `builder` measures
 * the largest argument struct and result of the host functions so far, and lays them out. */
static stasis_status init_group(struct host_group *group, struct group_builder *builder,
                                size_t index, const char *line) {
    const stasis_interface *interface_data = builder->layouts->walk->interface_data;
    const char *group_name = interface_data->groups[index].name;
    const int module_length = snprintf(NULL, 0, "%s:%s/%s@%s", interface_data->space,
                                       interface_data->name, group_name, line);
    if (module_length < 0) {
        return STASIS_INVALID_ARGUMENT;
    }
    group->module = stasis_allocate((size_t)module_length + 1);
    if (group->module == NULL) {
        return STASIS_OUT_OF_MEMORY;
    }
    (void)snprintf(group->module, (size_t)module_length + 1, "%s:%s/%s@%s", interface_data->space,
                   interface_data->name, group_name, line);

    builder->symbols = NULL;
    builder->names = NULL;
    builder->functions = NULL;
    builder->symbol_count = 0;
    builder->names_size = 0;
    add_group_symbols(builder, index);
    if (builder->symbol_count > UINT32_MAX) {
        return STASIS_INVALID_ARGUMENT;
    }
    const size_t measured = builder->symbol_count;
    if (measured == 0) {
        return STASIS_OK;
    }
    group->symbols = stasis_allocate_zeroed(measured, sizeof *group->symbols);
    group->names = stasis_allocate(builder->names_size);
    group->functions = stasis_allocate_zeroed(measured, sizeof *group->functions);
    if (group->symbols == NULL || group->names == NULL || group->functions == NULL) {
        return STASIS_OUT_OF_MEMORY;
    }
    builder->symbols = group->symbols;
    builder->names = group->names;
    builder->functions = group->functions;
    builder->symbol_count = 0;
    builder->names_size = 0;
    add_group_symbols(builder, index);
    /* Only the functions linked so far have layouts to free. */
    group->symbol_count = (uint32_t)builder->symbol_count;
    return builder->status;
}

stasis_status stasis_host_functions_init(struct host_functions *functions,
                                         struct layout_builder *layouts) {
    memset(functions, 0, sizeof *functions);
    const stasis_interface *interface_data = layouts->walk->interface_data;
    if (interface_data->group_count == 0) {
        return STASIS_OK;
    }
    functions->groups =
        stasis_allocate_zeroed(interface_data->group_count, sizeof *functions->groups);
    if (functions->groups == NULL) {
        return STASIS_OUT_OF_MEMORY;
    }
    functions->group_count = interface_data->group_count;

    char line[LINE_SIZE];
    if (interface_data->major == 0) {
        (void)snprintf(line, sizeof line, "0.%lu", (unsigned long)interface_data->minor);
    } else {
        (void)snprintf(line, sizeof line, "%lu", (unsigned long)interface_data->major);
    }
    struct group_builder builder;
    memset(&builder, 0, sizeof builder);
    builder.layouts = layouts;
    builder.status = STASIS_OK;
    for (size_t g = 0; g < functions->group_count; g++) {
        const stasis_status status = init_group(&functions->groups[g], &builder, g, line);
        if (status != STASIS_OK) {
            return status;
        }
    }

    /* The result goes after the largest argument struct, as aligned as any value that crosses. */
    const size_t alignment = _Alignof(union crossing);
    functions->result_offset = align_up(builder.largest_arguments, alignment);
    functions->scratch_size = functions->result_offset + builder.largest_result;
    return STASIS_OK;
}

/* Undoes the registration of the first `count` groups. */
static void unregister_groups(struct host_functions *functions, size_t count) {
    for (size_t g = 0; g < count; g++) {
        struct host_group *group = &functions->groups[g];
        if (group->symbol_count > 0) {
            (void)wasm_runtime_unregister_natives(group->module, group->symbols);
        }
    }
}

bool stasis_host_functions_register(struct host_functions *functions) {
    for (size_t g = 0; g < functions->group_count; g++) {
        struct host_group *group = &functions->groups[g];
        if (group->symbol_count > 0 && !wasm_runtime_register_natives_raw(
                                           group->module, group->symbols, group->symbol_count)) {
            unregister_groups(functions, g);
            return false;
        }
    }
    return true;
}

void stasis_host_functions_unregister(struct host_functions *functions) {
    unregister_groups(functions, functions->group_count);
}

void stasis_host_functions_free(struct host_functions *functions) {
    for (size_t g = 0; g < functions->group_count; g++) {
        struct host_group *group = &functions->groups[g];
        if (group->functions != NULL) {
            for (uint32_t i = 0; i < group->symbol_count; i++) {
                stasis_function_layout_free(&group->functions[i].layout);
            }
        }
        stasis_deallocate(group->module);
        stasis_deallocate(group->symbols);
        stasis_deallocate(group->names);
        stasis_deallocate(group->functions);
    }
    stasis_deallocate(functions->groups);
    memset(functions, 0, sizeof *functions);
}
