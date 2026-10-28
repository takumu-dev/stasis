#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "boundary.h"
#include "descriptors.h"
#include "objects.h"
#include "platform.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wasm_export.h>

/* The room for WAMR's message when instantiating fails, which Stasis reads no further. */
enum { WAMR_MESSAGE_SIZE = 128 };

/* Frees an instance's own memory, once WAMR's part of it is destroyed. */
static void free_instance(stasis_instance *instance) {
    stasis_deallocate((void *)instance->entry_points);
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
    if (created->entry_points == NULL) {
        free_instance(created);
        return STASIS_OUT_OF_MEMORY;
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

    created->script = script;
    created->user_data = user_data;
    created->state = INSTANCE_CREATED;
    if (script->initializes) {
        created->initialize =
            wasm_runtime_lookup_function(created->module_instance, STASIS_INITIALIZE_NAME);
    }
    for (size_t id = 0; id < script->entry_point_count; id++) {
        if (script->entry_points[id] != NULL) {
            created->entry_points[id] =
                wasm_runtime_lookup_function(created->module_instance, script->entry_points[id]);
        }
    }
    *instance = created;
    return STASIS_OK;
}

/* Runs a function of a ready instance with the core arguments given, and its results. A call that
 * traps leaves the instance trapped. */
static stasis_status run(stasis_instance *instance, wasm_function_inst_t function,
                         uint32_t result_count, wasm_val_t *results, uint32_t argument_count,
                         wasm_val_t *arguments) {
    instance->state = INSTANCE_RUNNING;
    if (!wasm_runtime_call_wasm_a(instance->exec_env, function, result_count, results,
                                  argument_count, arguments)) {
        instance->state = INSTANCE_TRAPPED;
        return STASIS_TRAP;
    }
    instance->state = INSTANCE_READY;
    return STASIS_OK;
}

stasis_status stasis_instance_start(stasis_instance *instance) {
    if (instance == NULL) {
        return STASIS_INVALID_ARGUMENT;
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
    return run(instance, instance->initialize, 0, NULL, 0, NULL);
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
    if (descriptor->parameter_count > STASIS_MAX_FLAT_PARAMETERS) {
        return STASIS_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < descriptor->parameter_count; i++) {
        if (!stasis_is_scalar(&descriptor->parameters[i])) {
            return STASIS_INVALID_ARGUMENT;
        }
    }
    if (descriptor->result != NULL && !stasis_is_scalar(descriptor->result)) {
        return STASIS_INVALID_ARGUMENT;
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

    wasm_val_t core_arguments[STASIS_MAX_FLAT_PARAMETERS];
    for (size_t i = 0; i < descriptor->parameter_count; i++) {
        stasis_lower_scalar(&descriptor->parameters[i], arguments, &core_arguments[i]);
    }
    wasm_val_t core_result;
    memset(&core_result, 0, sizeof core_result);
    const uint32_t result_count = descriptor->result != NULL ? 1 : 0;
    const stasis_status status = run(instance, function, result_count, &core_result,
                                     (uint32_t)descriptor->parameter_count, core_arguments);
    if (status == STASIS_OK && descriptor->result != NULL) {
        stasis_lift_scalar(descriptor->result, &core_result, result);
    }
    return status;
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
