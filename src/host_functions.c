#include "host_functions.h"

#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "boundary.h"
#include "descriptors.h"
/* NOLINTNEXTLINE(misc-include-cleaner): it defines the struct that stasis.h's type names */
#include "objects.h"
#include "signature.h"
#include "trap.h"

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

/* Whether Stasis can call a host function yet: when all its values are scalars, each of which
 * flattens to one core value. */
static bool callable(const stasis_host_function *function) {
    for (size_t i = 0; i < function->parameter_count; i++) {
        if (!stasis_is_scalar(&function->parameters[i])) {
            return false;
        }
    }
    if (function->result == NULL) {
        return true;
    }
    return stasis_is_scalar(function->result);
}

/* Ends the call that `instance` is running with the trap of Stasis's own it has raised: WAMR
 * unwinds the call once the trampoline returns, and the trap report reads the raised trap. */
static void end_call(stasis_instance *instance, wasm_exec_env_t exec_env) {
    wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env),
                               instance->raised.message != NULL ? instance->raised.message
                                                                : "Stasis raised a trap");
}

/* Lifts a host function's arguments from where the script spilled them in its memory, at
 * `address`, laid out as the canonical ABI lays out a tuple of them, into `arguments`. Returns
 * false, having raised a boundary violation, if they aren't all in the memory, or aren't aligned
 * as the strictest of them needs. */
static bool lift_spilled(stasis_instance *instance, wasm_exec_env_t exec_env,
                         const stasis_host_function *function, uint32_t address,
                         unsigned char *arguments) {
    size_t size = 0;
    size_t alignment = 1;
    for (size_t i = 0; i < function->parameter_count; i++) {
        const size_t field = stasis_scalar_memory_size(&function->parameters[i]);
        size = align_up(size, field) + field;
        if (field > alignment) {
            alignment = field;
        }
    }
    size = align_up(size, alignment);

    wasm_memory_inst_t memory =
        wasm_runtime_get_default_memory(wasm_runtime_get_module_inst(exec_env));
    const uint64_t memory_size = memory == NULL ? 0
                                                : wasm_memory_get_cur_page_count(memory) *
                                                      wasm_memory_get_bytes_per_page(memory);
    if (address % alignment != 0) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed the arguments of `%s` at %lu in its memory, "
                                "which isn't a multiple of their alignment, %lu",
                                function->name, (unsigned long)address, (unsigned long)alignment);
        return false;
    }
    if ((uint64_t)address + size > memory_size) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed the arguments of `%s` at %lu in its memory, "
                                "where their %lu bytes run past its end at %llu",
                                function->name, (unsigned long)address, (unsigned long)size,
                                (unsigned long long)memory_size);
        return false;
    }

    const unsigned char *at = (const unsigned char *)wasm_memory_get_base_address(memory) + address;
    size_t offset = 0;
    for (size_t i = 0; i < function->parameter_count; i++) {
        const size_t field = stasis_scalar_memory_size(&function->parameters[i]);
        offset = align_up(offset, field);
        stasis_load_scalar(&function->parameters[i], at + offset, arguments);
        offset += field;
    }
    return true;
}

/* What WAMR calls in place of every host function, in its raw calling convention: with the script's
 * core arguments, each in a slot of its own, and the slot that the result goes in first. The host
 * function's descriptor is its attachment, and the instance is its execution environment's user
 * data. */
static void trampoline(wasm_exec_env_t exec_env, uint64_t *core) {
    /* WAMR sets the attachment to null once a host function calls back into the script, so it's
     * read first. */
    const stasis_host_function *function = wasm_runtime_get_function_attachment(exec_env);
    stasis_instance *instance = wasm_runtime_get_user_data(exec_env);
    unsigned char *arguments = instance->scratch;
    unsigned char *result = arguments + instance->script->host_functions.result_offset;
    /* Every value Stasis can pass yet is a scalar, which flattens to one core value, so the
     * arguments spill into memory when there are more of them than core parameters. */
    if (function->parameter_count > STASIS_MAX_FLAT_PARAMETERS) {
        if (!lift_spilled(instance, exec_env, function, (uint32_t)core[0], arguments)) {
            end_call(instance, exec_env);
            return;
        }
    } else {
        for (size_t i = 0; i < function->parameter_count; i++) {
            stasis_lift_raw_scalar(&function->parameters[i], &core[i], arguments);
        }
    }
    function->thunk(instance, function->parameter_count > 0 ? arguments : NULL,
                    function->result != NULL ? result : NULL);
    /* A host function that aborted the call has returned all the same, and its result is
     * ignored. */
    if (instance->raised.raised) {
        end_call(instance, exec_env);
        return;
    }
    if (function->result != NULL) {
        stasis_lower_raw_scalar(function->result, result, &core[0]);
    }
}

/* WAMR takes a host function and its attachment as object pointers, which ISO C can't convert a
 * function pointer or a pointer to const to, so each is read back through a union. */

static void *trampoline_pointer(void) {
    union {
        void (*function)(wasm_exec_env_t, uint64_t *);
        void *object;
    } pointer = {trampoline};

    return pointer.object;
}

static void *attachment_of(const stasis_host_function *function) {
    union {
        const stasis_host_function *descriptor;
        void *object;
    } attachment = {function};

    return attachment.object;
}

/* A group's symbols being made: measured first, with `symbols` null, and then written. It goes on
 * from group to group, measuring the scratch an instance needs. */
struct group_builder {
    struct descriptor_walk *walk;
    NativeSymbol *symbols;
    char *names;
    size_t symbol_count;
    size_t names_size;
    /* The largest argument struct and result of the host functions measured. */
    size_t largest_arguments;
    size_t largest_result;
    /* Whether a descriptor was found malformed. */
    bool malformed;
};

/* Adds the symbol of a host function, or of a method of the kind of host object called `object`,
 * if Stasis can call it. Its name is `<function>:<hash>`, or `<object>.<method>:<hash>`. */
static void add_symbol(struct group_builder *builder, const char *object,
                       const stasis_host_function *function) {
    if (!callable(function)) {
        return;
    }
    const size_t object_size = object == NULL ? 0 : strlen(object) + 1;
    const size_t size = object_size + strlen(function->name) + 1 + STASIS_SIGNATURE_HASH_LENGTH + 1;
    if (builder->symbols != NULL) {
        char hash[STASIS_SIGNATURE_HASH_LENGTH + 1];
        if (!stasis_host_function_hash(builder->walk, function, hash)) {
            builder->malformed = true;
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
        symbol->func_ptr = trampoline_pointer();
        symbol->signature = NULL;
        symbol->attachment = attachment_of(function);
    }
    builder->symbol_count++;
    builder->names_size += size;
    if (function->arguments_size > builder->largest_arguments) {
        builder->largest_arguments = function->arguments_size;
    }
    if (function->result != NULL) {
        const size_t result_size =
            stasis_value_size(builder->walk->interface_data, function->result);
        if (result_size > builder->largest_result) {
            builder->largest_result = result_size;
        }
    }
}

/* Adds the symbols of the group at `index`: its host functions, then its kinds' methods. */
static void add_group_symbols(struct group_builder *builder, size_t index) {
    const stasis_interface *interface_data = builder->walk->interface_data;
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
 * the largest argument struct and result of the host functions so far, and has the walk. */
static stasis_status init_group(struct host_group *group, struct group_builder *builder,
                                size_t index, const char *line) {
    const stasis_interface *interface_data = builder->walk->interface_data;
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
    builder->symbol_count = 0;
    builder->names_size = 0;
    add_group_symbols(builder, index);
    if (builder->symbol_count > UINT32_MAX) {
        return STASIS_INVALID_ARGUMENT;
    }
    group->symbol_count = (uint32_t)builder->symbol_count;
    if (group->symbol_count == 0) {
        return STASIS_OK;
    }
    group->symbols = stasis_allocate_zeroed(group->symbol_count, sizeof *group->symbols);
    group->names = stasis_allocate(builder->names_size);
    if (group->symbols == NULL || group->names == NULL) {
        return STASIS_OUT_OF_MEMORY;
    }
    builder->symbols = group->symbols;
    builder->names = group->names;
    builder->symbol_count = 0;
    builder->names_size = 0;
    add_group_symbols(builder, index);
    return builder->malformed ? STASIS_INVALID_ARGUMENT : STASIS_OK;
}

stasis_status stasis_host_functions_init(struct host_functions *functions,
                                         struct descriptor_walk *walk) {
    memset(functions, 0, sizeof *functions);
    const stasis_interface *interface_data = walk->interface_data;
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
    builder.walk = walk;
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
        stasis_deallocate(functions->groups[g].module);
        stasis_deallocate(functions->groups[g].symbols);
        stasis_deallocate(functions->groups[g].names);
    }
    stasis_deallocate(functions->groups);
    memset(functions, 0, sizeof *functions);
}
