#include <stasis.h>

#include "allocator.h"
/* NOLINTNEXTLINE(misc-include-cleaner): it defines the struct that stasis.h's type names */
#include "objects.h"
#include "platform.h"
#include "system_interface.h"
#include "wamr_hooks.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wasm_export.h>

/* The number of libraries that exist. WAMR's state is the process's, so the first library to be
 * created initialises it, and the last to be destroyed tears it down. It changes only under
 * Stasis's lock. */
static size_t library_count;

stasis_status stasis_library_create(const stasis_library_settings *settings,
                                    stasis_library **library) {
    if (library == NULL) {
        return STASIS_INVALID_ARGUMENT;
    }
    *library = NULL;
    stasis_library_settings resolved = {0};
    if (settings != NULL) {
        resolved = *settings;
    }
    if (resolved.stack_size > UINT32_MAX) {
        return STASIS_INVALID_ARGUMENT;
    }

    stasis_library *created = stasis_allocate(sizeof *created);
    if (created == NULL) {
        return STASIS_OUT_OF_MEMORY;
    }
    created->settings = resolved;
    created->stack_size =
        resolved.stack_size == 0 ? STASIS_DEFAULT_STACK_SIZE : (uint32_t)resolved.stack_size;
    created->script_count = 0;

    bool initialised = true;
    stasis_lock();
    if (library_count == 0) {
        RuntimeInitArgs args;
        stasis_wamr_init_args(&args);
        initialised = wasm_runtime_full_init(&args);
        if (initialised && !stasis_system_interface_register()) {
            wasm_runtime_destroy();
            initialised = false;
        }
    }
    if (initialised) {
        library_count++;
    }
    stasis_unlock();
    if (!initialised) {
        stasis_deallocate(created);
        return STASIS_OUT_OF_MEMORY;
    }
    *library = created;
    return STASIS_OK;
}

stasis_status stasis_library_destroy(stasis_library *library) {
    if (library == NULL) {
        return STASIS_OK;
    }
    stasis_lock();
    if (library->script_count > 0) {
        stasis_unlock();
        return STASIS_REFUSED_IN_USE;
    }
    library_count--;
    if (library_count == 0) {
        wasm_runtime_destroy();
    }
    stasis_unlock();
    stasis_deallocate(library);
    return STASIS_OK;
}
