#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "boundary.h"
#include "handles.h"
#include "instance.h"
#include "message.h"
#include "module.h"
#include "objects.h"
#include "snapshot_format.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wasm_export.h>

/* Why an instance can't be snapshotted, if it can't: its script isn't snapshottable, or it isn't
 * ready. */
static stasis_status refusal(const stasis_instance *instance) {
    if (instance->script->unsnapshottable != NULL) {
        return STASIS_REFUSED_NOT_SNAPSHOTTABLE;
    }
    switch (instance->state) {
    case INSTANCE_READY:
        break;
    case INSTANCE_CREATED:
        return STASIS_REFUSED_NOT_STARTED;
    case INSTANCE_RUNNING:
        return STASIS_REFUSED_RUNNING;
    case INSTANCE_TRAPPED:
        return STASIS_REFUSED_TRAPPED;
    }
    return STASIS_OK;
}

/* An instance's state, as a snapshot holds it, which points into the instance as it is now. */
static struct snapshot_state state_of(const stasis_instance *instance) {
    const struct script_memory memory = stasis_script_memory(instance->module_instance);
    const struct snapshot_state state = {memory.base, memory.size, instance->global_values,
                                         &instance->handles};
    return state;
}

stasis_status stasis_instance_snapshot_size(const stasis_instance *instance, size_t *size) {
    if (size != NULL) {
        *size = 0;
    }
    if (instance == NULL || size == NULL) {
        return STASIS_INVALID_ARGUMENT;
    }
    const stasis_status refused = refusal(instance);
    if (refused != STASIS_OK) {
        return refused;
    }
    const struct snapshot_state state = state_of(instance);
    *size = stasis_snapshot_size(&instance->script->snapshot_rules, &state);
    return STASIS_OK;
}

stasis_status stasis_instance_snapshot(const stasis_instance *instance, void *buffer,
                                       size_t buffer_size, size_t *size) {
    if (size != NULL) {
        *size = 0;
    }
    if (instance == NULL || size == NULL || (buffer == NULL && buffer_size > 0)) {
        return STASIS_INVALID_ARGUMENT;
    }
    const stasis_status refused = refusal(instance);
    if (refused != STASIS_OK) {
        return refused;
    }
    const struct snapshot_state state = state_of(instance);
    const size_t needed = stasis_snapshot_size(&instance->script->snapshot_rules, &state);
    if (buffer_size < needed) {
        return STASIS_BUFFER_TOO_SMALL;
    }
    stasis_snapshot_write(&instance->script->snapshot_rules, &state, buffer);
    *size = needed;
    return STASIS_OK;
}

stasis_status stasis_instance_handle_count(const stasis_instance *instance, size_t *count) {
    if (count != NULL) {
        *count = 0;
    }
    if (instance == NULL || count == NULL) {
        return STASIS_INVALID_ARGUMENT;
    }
    *count = instance->handles.count;
    return STASIS_OK;
}

stasis_status stasis_instance_handles(const stasis_instance *instance, stasis_handle *handles,
                                      size_t capacity, size_t *count) {
    if (count != NULL) {
        *count = 0;
    }
    if (instance == NULL || count == NULL || (handles == NULL && capacity > 0)) {
        return STASIS_INVALID_ARGUMENT;
    }
    const struct handle_table *table = &instance->handles;
    if (capacity < table->count) {
        return STASIS_BUFFER_TOO_SMALL;
    }
    for (size_t i = 0; i < table->count; i++) {
        const struct handle handle = stasis_handle_at(table, i);
        handles[i].value = handle.value;
        handles[i].kind = handle.kind;
        handles[i].host_object = handle.object;
    }
    *count = table->count;
    return STASIS_OK;
}

/* A restore in progress, once the snapshot has passed the reader's checks: the snapshot's live
 * handles, in increasing order of value, and the host's entry that binds each, or null while none
 * has. */
struct restore {
    stasis_script *script;
    struct snapshot_reading reading;
    struct snapshot_handle *live;
    const stasis_handle **given;
    struct message *message;
};

/* The position among the snapshot's live handles of the one with the value `value`, or SIZE_MAX
 * if none has it. */
static size_t find_live(const struct restore *restore, uint32_t value) {
    size_t low = 0;
    size_t high = restore->reading.handle_count;
    while (low < high) {
        const size_t middle = low + ((high - low) / 2);
        if (restore->live[middle].value == value) {
            return middle;
        }
        if (restore->live[middle].value < value) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return SIZE_MAX;
}

/* The qualified name of the kind of host object `kind` of the script's interface. */
static const char *kind_name(const struct restore *restore, uint32_t kind) {
    return restore->script->interface_data->kinds[kind].qualified_name;
}

/* Matches each of the host's entries in `handles` to the snapshot's live handle it binds. Returns
 * false, having noted the problem, if one binds a handle that isn't live, or one that another has
 * bound, or gives it a host object of another kind, or if a live handle is left out. */
static bool match_handles(struct restore *restore, const stasis_handle *handles, size_t count) {
    const stasis_interface *interface_data = restore->script->interface_data;
    for (size_t i = 0; i < count; i++) {
        const stasis_handle *entry = &handles[i];
        const size_t position = find_live(restore, entry->value);
        if (position == SIZE_MAX) {
            stasis_message_add(restore->message,
                               "the handles give handle %lu, which isn't live in the snapshot",
                               (unsigned long)entry->value);
            return false;
        }
        const struct snapshot_handle *live = &restore->live[position];
        if (restore->given[position] != NULL) {
            stasis_message_add(restore->message,
                               "the handles give handle %lu, of the kind `%s`, twice",
                               (unsigned long)live->value, kind_name(restore, live->kind));
            return false;
        }
        if (entry->kind != live->kind) {
            if (entry->kind < interface_data->kind_count) {
                stasis_message_add(restore->message,
                                   "the handles give handle %lu, of the kind `%s`, a host object "
                                   "of the kind `%s`",
                                   (unsigned long)live->value, kind_name(restore, live->kind),
                                   kind_name(restore, entry->kind));
            } else {
                stasis_message_add(restore->message,
                                   "the handles give handle %lu, of the kind `%s`, a host object "
                                   "of kind %lu, which the interface lacks",
                                   (unsigned long)live->value, kind_name(restore, live->kind),
                                   (unsigned long)entry->kind);
            }
            return false;
        }
        restore->given[position] = entry;
    }
    for (uint32_t i = 0; i < restore->reading.handle_count; i++) {
        if (restore->given[i] == NULL) {
            const struct snapshot_handle *live = &restore->live[i];
            stasis_message_add(restore->message,
                               "the handles leave out handle %lu, of the kind `%s`",
                               (unsigned long)live->value, kind_name(restore, live->kind));
            return false;
        }
    }
    return true;
}

/* Fills in the handle table of the restored instance, which is empty: the handles the snapshot
 * records as issued, and each live one that the host binds to a host object, rather than revoking
 * it. Returns STASIS_OK, STASIS_OUT_OF_MEMORY, or STASIS_INVALID_BINDINGS, having noted the
 * problem, if the host binds two handles to the same host object. */
static stasis_status bind_handles(const struct restore *restore, struct handle_table *table) {
    size_t bound = 0;
    for (uint32_t i = 0; i < restore->reading.handle_count; i++) {
        if (restore->given[i]->host_object != NULL) {
            bound++;
        }
    }
    /* The next handle value is at most one past the last that can be issued, which fits. */
    const uint32_t issued = (uint32_t)(restore->reading.next_handle - 1);
    if (!stasis_handle_table_restore(table, issued, bound)) {
        return STASIS_OUT_OF_MEMORY;
    }
    for (uint32_t i = 0; i < restore->reading.handle_count; i++) {
        const struct snapshot_handle *live = &restore->live[i];
        void *object = restore->given[i]->host_object;
        if (object == NULL) {
            continue;
        }
        const uint32_t other = stasis_handle_of(table, live->kind, object);
        if (other != 0) {
            stasis_message_add(restore->message,
                               "the handles give handles %lu and %lu, of the kind `%s`, the same "
                               "host object",
                               (unsigned long)other, (unsigned long)live->value,
                               kind_name(restore, live->kind));
            return STASIS_INVALID_BINDINGS;
        }
        stasis_handle_bind(table, live->value, live->kind, object);
    }
    return STASIS_OK;
}

/* Puts the memory and the globals of the snapshot into a new instance of its script, whose memory
 * starts no larger than the snapshot's. Returns false if there's no memory for the snapshot's. */
static bool put_state(stasis_instance *instance, const struct snapshot_reading *reading) {
    wasm_memory_inst_t memory = wasm_runtime_get_default_memory(instance->module_instance);
    const uint64_t pages = reading->memory_size / MODULE_PAGE_SIZE;
    const uint64_t current = wasm_memory_get_cur_page_count(memory);
    if (pages > current &&
        !wasm_runtime_enlarge_memory(instance->module_instance, pages - current)) {
        return false;
    }
    if (reading->memory_size > 0) {
        const struct script_memory grown = stasis_script_memory(instance->module_instance);
        memcpy(grown.base, reading->memory, (size_t)reading->memory_size);
    }
    stasis_snapshot_globals(reading, &instance->script->snapshot_rules, instance->global_values);
    return true;
}

/* Restores a snapshot that has passed the reader's checks into a new instance, as
 * stasis_instance_restore does, once the host's handles have been matched to its live handles. */
static stasis_status restore_instance(const struct restore *restore, void *user_data,
                                      stasis_instance **instance) {
    struct handle_table table;
    stasis_handle_table_init(&table);
    stasis_status status = bind_handles(restore, &table);
    if (status != STASIS_OK) {
        stasis_handle_table_free(&table);
        return status;
    }
    stasis_instance *created = NULL;
    status = stasis_instance_new(restore->script, user_data, &created);
    if (status != STASIS_OK) {
        stasis_handle_table_free(&table);
        return status;
    }
    if (!put_state(created, &restore->reading)) {
        stasis_handle_table_free(&table);
        (void)stasis_instance_destroy(created);
        return STASIS_OUT_OF_MEMORY;
    }
    stasis_handle_table_free(&created->handles);
    created->handles = table;
    created->state = INSTANCE_READY;
    *instance = created;
    return STASIS_OK;
}

stasis_status stasis_instance_restore(stasis_script *script, const void *snapshot, size_t size,
                                      const stasis_handle *handles, size_t handle_count,
                                      void *user_data, char *message, size_t message_size,
                                      stasis_instance **instance) {
    if (instance != NULL) {
        *instance = NULL;
    }
    if (message == NULL && message_size > 0) {
        return STASIS_INVALID_ARGUMENT;
    }
    struct message written;
    stasis_message_init(&written, message, message_size);
    if (script == NULL || instance == NULL || (snapshot == NULL && size > 0) ||
        (handles == NULL && handle_count > 0)) {
        return STASIS_INVALID_ARGUMENT;
    }
    if (script->unsnapshottable != NULL) {
        return STASIS_REFUSED_NOT_SNAPSHOTTABLE;
    }

    struct restore restore;
    memset(&restore, 0, sizeof restore);
    restore.script = script;
    restore.message = &written;
    stasis_status status =
        stasis_snapshot_read(&restore.reading, &script->snapshot_rules, snapshot, size, &written);
    if (status != STASIS_OK) {
        return status;
    }
    const uint32_t count = restore.reading.handle_count;
    restore.live = stasis_allocate(count * sizeof *restore.live);
    restore.given = (const stasis_handle **)stasis_allocate_zeroed(count, sizeof *restore.given);
    if (restore.live == NULL || restore.given == NULL) {
        status = STASIS_OUT_OF_MEMORY;
    } else {
        stasis_snapshot_handles(&restore.reading, &script->snapshot_rules, restore.live);
        if (!match_handles(&restore, handles, handle_count)) {
            status = STASIS_INVALID_BINDINGS;
        } else {
            status = restore_instance(&restore, user_data, instance);
        }
    }
    stasis_deallocate(restore.live);
    stasis_deallocate((void *)restore.given);
    return status;
}
