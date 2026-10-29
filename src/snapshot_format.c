#include "snapshot_format.h"

#include <stasis.h>
#include <stasis_interface.h>

#include "handles.h"
#include "message.h"
#include "module.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The magic number that every snapshot starts with. */
static const unsigned char magic[8] = {0x00, 's', 't', 'a', 's', 'i', 's', 0x00};

/* The version of the format that this release writes, which is the newest it reads. Each version
 * only adds to the last, so a release reads every version up to its own. */
enum { FORMAT_VERSION = 1 };

/* The most bytes of a kind's name in a snapshot that a message prints, since the snapshot may hold
 * any bytes there. */
enum { PRINTED_NAME_SIZE = 64 };

/* The room for the start of an identity as hex, which a message prints, and its NUL. */
enum { IDENTITY_PREFIX_SIZE = 17 };

/* The size of a global's value, by its type. */
static size_t value_size(uint8_t type) { return type == MODULE_I64 || type == MODULE_F64 ? 8 : 4; }

/* Each writes a field little-endian at `at`, and returns where the next field goes. */

static unsigned char *put_u32(unsigned char *at, uint32_t value) {
    for (size_t i = 0; i < 4; i++) {
        at[i] = (unsigned char)(value >> (8 * i) & 0xff);
    }
    return at + 4;
}

static unsigned char *put_u64(unsigned char *at, uint64_t value) {
    for (size_t i = 0; i < 8; i++) {
        at[i] = (unsigned char)(value >> (8 * i) & 0xff);
    }
    return at + 8;
}

static unsigned char *put_bytes(unsigned char *at, const void *bytes, size_t size) {
    if (size > 0) {
        memcpy(at, bytes, size);
    }
    return at + size;
}

/* Each reads a field written little-endian at `at`. */

static uint32_t get_u32(const unsigned char *at) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; i++) {
        value |= (uint32_t)at[i] << (8 * i);
    }
    return value;
}

static uint64_t get_u64(const unsigned char *at) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++) {
        value |= (uint64_t)at[i] << (8 * i);
    }
    return value;
}

/* Writes the value of a global of the type `type`, which `value` holds in the host's byte order,
 * at `at`, and returns where the next field goes. */
static unsigned char *put_value(unsigned char *at, uint8_t type, const void *value) {
    if (value_size(type) == 8) {
        uint64_t bits = 0;
        memcpy(&bits, value, sizeof bits);
        return put_u64(at, bits);
    }
    uint32_t bits = 0;
    memcpy(&bits, value, sizeof bits);
    return put_u32(at, bits);
}

/* Reads the value of a global of the type `type` at `at` into `value`, in the host's byte order,
 * and returns where the next field is. */
static const unsigned char *get_value(const unsigned char *at, uint8_t type, void *value) {
    if (value_size(type) == 8) {
        const uint64_t bits = get_u64(at);
        memcpy(value, &bits, sizeof bits);
        return at + 8;
    }
    const uint32_t bits = get_u32(at);
    memcpy(value, &bits, sizeof bits);
    return at + 4;
}

size_t stasis_snapshot_size(const struct snapshot_script *script,
                            const struct snapshot_state *state) {
    size_t size = sizeof magic + 4 + STASIS_IDENTITY_SIZE + 8 + 4;
    for (size_t i = 0; i < script->global_count; i++) {
        size += 4 + 1 + value_size(script->globals[i].type);
    }
    size += 8 + 4;
    for (size_t i = 0; i < state->handles->count; i++) {
        const struct handle handle = stasis_handle_at(state->handles, i);
        size += 4 + 4 + strlen(script->interface_data->kinds[handle.kind].qualified_name);
    }
    return size + (size_t)state->memory_size;
}

void stasis_snapshot_write(const struct snapshot_script *script, const struct snapshot_state *state,
                           unsigned char *buffer) {
    unsigned char *at = put_bytes(buffer, magic, sizeof magic);
    at = put_u32(at, FORMAT_VERSION);
    at = put_bytes(at, script->identity, STASIS_IDENTITY_SIZE);
    at = put_u64(at, state->memory_size);

    at = put_u32(at, (uint32_t)script->global_count);
    for (size_t i = 0; i < script->global_count; i++) {
        const struct snapshot_global *global = &script->globals[i];
        at = put_u32(at, global->index);
        *at++ = global->type;
        at = put_value(at, global->type, state->global_values[i]);
    }

    const struct handle_table *handles = state->handles;
    at = put_u64(at, (uint64_t)handles->issued + 1);
    at = put_u32(at, (uint32_t)handles->count);
    for (size_t i = 0; i < handles->count; i++) {
        const struct handle handle = stasis_handle_at(handles, i);
        const char *name = script->interface_data->kinds[handle.kind].qualified_name;
        const size_t name_size = strlen(name);
        at = put_u32(at, handle.value);
        at = put_u32(at, (uint32_t)name_size);
        at = put_bytes(at, name, name_size);
    }
    (void)put_bytes(at, state->memory, (size_t)state->memory_size);
}

/* Reads a snapshot's fields in turn, from `at`, which `left` bytes follow. Once a field runs past
 * the end, it has failed, and every later field reads as zeros. */
struct reader {
    const unsigned char *at;
    size_t left;
    bool failed;
};

/* Takes the next `size` bytes, or returns null, having failed, if fewer are left. */
static const unsigned char *take(struct reader *reader, size_t size) {
    if (reader->failed || size > reader->left) {
        reader->failed = true;
        return NULL;
    }
    const unsigned char *taken = reader->at;
    reader->at += size;
    reader->left -= size;
    return taken;
}

/* Each takes the next field, written little-endian, or returns 0, having failed, if it doesn't
 * fit. */

static uint8_t take_u8(struct reader *reader) {
    const unsigned char *at = take(reader, 1);
    return at == NULL ? 0 : at[0];
}

static uint32_t take_u32(struct reader *reader) {
    const unsigned char *at = take(reader, 4);
    return at == NULL ? 0 : get_u32(at);
}

static uint64_t take_u64(struct reader *reader) {
    const unsigned char *at = take(reader, 8);
    return at == NULL ? 0 : get_u64(at);
}

/* Notes that the snapshot is cut short in `part`, if the reader has failed. Returns whether it
 * has. */
static bool ended(const struct reader *reader, struct message *message, const char *part) {
    if (reader->failed) {
        stasis_message_add(message, "the snapshot is cut short in its %s", part);
    }
    return reader->failed;
}

/* Writes the first bytes of `identity` as hex, into `text`, which holds IDENTITY_PREFIX_SIZE. */
static void identity_prefix(const unsigned char *identity, char *text) {
    for (size_t i = 0; i < (IDENTITY_PREFIX_SIZE - 1) / 2; i++) {
        (void)snprintf(text + (2 * i), 3, "%02x", identity[i]);
    }
}

/* Reads the snapshot's header: its magic number, its format's version and its script's
 * identity. */
static stasis_status read_header(struct reader *reader, const struct snapshot_script *script,
                                 struct message *message) {
    const unsigned char *start = take(reader, sizeof magic);
    if (start == NULL || memcmp(start, magic, sizeof magic) != 0) {
        stasis_message_add(message, "the bytes don't start with a snapshot's magic number");
        return STASIS_INVALID_SNAPSHOT;
    }
    const uint32_t version = take_u32(reader);
    if (ended(reader, message, "format's version")) {
        return STASIS_INVALID_SNAPSHOT;
    }
    if (version == 0) {
        stasis_message_add(message,
                           "the snapshot's format is version 0, which no release of Stasis writes");
        return STASIS_INVALID_SNAPSHOT;
    }
    if (version > FORMAT_VERSION) {
        stasis_message_add(message,
                           "the snapshot's format is version %lu, and this release of Stasis reads "
                           "versions up to %lu",
                           (unsigned long)version, (unsigned long)FORMAT_VERSION);
        return STASIS_SNAPSHOT_TOO_NEW;
    }
    const unsigned char *identity = take(reader, STASIS_IDENTITY_SIZE);
    if (ended(reader, message, "script's identity")) {
        return STASIS_INVALID_SNAPSHOT;
    }
    if (memcmp(identity, script->identity, STASIS_IDENTITY_SIZE) != 0) {
        char theirs[IDENTITY_PREFIX_SIZE];
        char ours[IDENTITY_PREFIX_SIZE];
        identity_prefix(identity, theirs);
        identity_prefix(script->identity, ours);
        stasis_message_add(message,
                           "the snapshot is of the script whose identity starts %s, not of this "
                           "one, whose identity starts %s",
                           theirs, ours);
        return STASIS_WRONG_SCRIPT;
    }
    return STASIS_OK;
}

/* Reads the length of the snapshot's memory, which must be a whole number of pages within the
 * limits of the script's memory. */
static bool read_memory_size(struct reader *reader, const struct snapshot_script *script,
                             struct message *message, uint64_t *size) {
    *size = take_u64(reader);
    if (ended(reader, message, "memory's length")) {
        return false;
    }
    if (*size % MODULE_PAGE_SIZE != 0) {
        stasis_message_add(message,
                           "the snapshot's memory is %llu bytes, which isn't a whole number of "
                           "pages of %lu bytes",
                           (unsigned long long)*size, (unsigned long)MODULE_PAGE_SIZE);
        return false;
    }
    const uint64_t pages = *size / MODULE_PAGE_SIZE;
    if (pages < script->minimum_pages || pages > script->maximum_pages) {
        stasis_message_add(message,
                           "the snapshot's memory is %llu pages, where the script's memory has "
                           "from %lu to %lu",
                           (unsigned long long)pages, (unsigned long)script->minimum_pages,
                           (unsigned long)script->maximum_pages);
        return false;
    }
    return true;
}

/* Reads the snapshot's globals, which must be the script's mutable globals, each of its type. */
static bool read_globals(struct reader *reader, const struct snapshot_script *script,
                         struct message *message) {
    const uint32_t count = take_u32(reader);
    if (ended(reader, message, "globals")) {
        return false;
    }
    if (count != script->global_count) {
        stasis_message_add(message,
                           "the snapshot holds %lu mutable globals, where the script defines %lu",
                           (unsigned long)count, (unsigned long)script->global_count);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        const struct snapshot_global *expected = &script->globals[i];
        const uint32_t index = take_u32(reader);
        const uint8_t type = take_u8(reader);
        if (ended(reader, message, "globals")) {
            return false;
        }
        if (index != expected->index) {
            stasis_message_add(message,
                               "the snapshot's mutable global %lu is global %lu, where the "
                               "script's is global %lu",
                               (unsigned long)i, (unsigned long)index,
                               (unsigned long)expected->index);
            return false;
        }
        if (type != expected->type) {
            stasis_message_add(message,
                               "the snapshot's global %lu is of type %s, where the script's is of "
                               "type %s",
                               (unsigned long)index, stasis_module_type_name(type),
                               stasis_module_type_name(expected->type));
            return false;
        }
        (void)take(reader, value_size(type));
        if (ended(reader, message, "globals")) {
            return false;
        }
    }
    return true;
}

/* The kind of host object of the interface whose qualified name is the `size` bytes at `name`, or
 * SIZE_MAX if it has none. */
static size_t find_kind(const stasis_interface *interface_data, const unsigned char *name,
                        size_t size) {
    for (size_t k = 0; k < interface_data->kind_count; k++) {
        const char *qualified_name = interface_data->kinds[k].qualified_name;
        if (strlen(qualified_name) == size && memcmp(qualified_name, name, size) == 0) {
            return k;
        }
    }
    return SIZE_MAX;
}

/* Reads one of the snapshot's live handles, which must follow the last, `previous`, in
 * increasing order of value, be less than the next handle value, and name a kind of host object
 * of the interface, and sets `previous` to it. */
static bool read_handle(struct reader *reader, const struct snapshot_script *script,
                        struct message *message, uint64_t next, uint32_t *previous) {
    const uint32_t value = take_u32(reader);
    const uint32_t name_size = take_u32(reader);
    const unsigned char *name = take(reader, name_size);
    if (ended(reader, message, "handles")) {
        return false;
    }
    if (value == 0 || value >= next) {
        stasis_message_add(message,
                           "the snapshot's live handle %lu is outside the values it records as "
                           "issued, from 1 to %llu",
                           (unsigned long)value, (unsigned long long)(next - 1));
        return false;
    }
    if (value <= *previous) {
        stasis_message_add(message,
                           "the snapshot's live handle %lu follows handle %lu, out of increasing "
                           "order of value",
                           (unsigned long)value, (unsigned long)*previous);
        return false;
    }
    *previous = value;
    const stasis_interface *interface_data = script->interface_data;
    if (find_kind(interface_data, name, name_size) == SIZE_MAX) {
        stasis_message_add(message,
                           "the snapshot's live handle %lu is of the kind `%.*s`, which "
                           "`%s:%s@%lu.%lu.%lu` lacks",
                           (unsigned long)value,
                           (int)(name_size > PRINTED_NAME_SIZE ? PRINTED_NAME_SIZE : name_size),
                           (const char *)name, interface_data->space, interface_data->name,
                           (unsigned long)interface_data->major,
                           (unsigned long)interface_data->minor,
                           (unsigned long)interface_data->patch);
        return false;
    }
    return true;
}

/* Reads the snapshot's handle table: the next handle value, which must be one an instance can
 * reach, and the live handles, which must be fewer than the handles it records as issued. */
static bool read_handles(struct reader *reader, const struct snapshot_script *script,
                         struct message *message, struct snapshot_reading *reading) {
    reading->next_handle = take_u64(reader);
    reading->handle_count = take_u32(reader);
    if (ended(reader, message, "handles")) {
        return false;
    }
    const uint64_t most_next = (uint64_t)STASIS_HANDLE_LIMIT + 1;
    if (reading->next_handle == 0 || reading->next_handle > most_next) {
        stasis_message_add(message,
                           "the snapshot's next handle value is %llu, where it can be from 1 to "
                           "%llu",
                           (unsigned long long)reading->next_handle, (unsigned long long)most_next);
        return false;
    }
    if (reading->handle_count > reading->next_handle - 1) {
        stasis_message_add(message,
                           "the snapshot holds %lu live handles, more than the %llu it records as "
                           "issued",
                           (unsigned long)reading->handle_count,
                           (unsigned long long)(reading->next_handle - 1));
        return false;
    }
    reading->handles = reader->at;
    uint32_t previous = 0;
    for (uint32_t i = 0; i < reading->handle_count; i++) {
        if (!read_handle(reader, script, message, reading->next_handle, &previous)) {
            return false;
        }
    }
    return true;
}

stasis_status stasis_snapshot_read(struct snapshot_reading *reading,
                                   const struct snapshot_script *script, const unsigned char *bytes,
                                   size_t size, struct message *message) {
    memset(reading, 0, sizeof *reading);
    struct reader reader = {bytes, size, false};
    const stasis_status header = read_header(&reader, script, message);
    if (header != STASIS_OK) {
        return header;
    }
    if (!read_memory_size(&reader, script, message, &reading->memory_size)) {
        return STASIS_INVALID_SNAPSHOT;
    }
    reading->globals = reader.at;
    if (!read_globals(&reader, script, message) ||
        !read_handles(&reader, script, message, reading)) {
        return STASIS_INVALID_SNAPSHOT;
    }
    if (reader.left != reading->memory_size) {
        stasis_message_add(message,
                           "the snapshot is %llu bytes long, where the length of its memory makes "
                           "it %llu",
                           (unsigned long long)size,
                           (unsigned long long)(size - reader.left) + reading->memory_size);
        return STASIS_INVALID_SNAPSHOT;
    }
    reading->memory = reader.at;
    return STASIS_OK;
}

void stasis_snapshot_handles(const struct snapshot_reading *reading,
                             const struct snapshot_script *script,
                             struct snapshot_handle *handles) {
    const unsigned char *at = reading->handles;
    for (uint32_t i = 0; i < reading->handle_count; i++) {
        const uint32_t name_size = get_u32(at + 4);
        handles[i].value = get_u32(at);
        handles[i].kind = (uint32_t)find_kind(script->interface_data, at + 8, name_size);
        at += 8 + name_size;
    }
}

void stasis_snapshot_globals(const struct snapshot_reading *reading,
                             const struct snapshot_script *script, void *const *global_values) {
    /* The globals follow their count, each its index, its type and its value. */
    const unsigned char *at = reading->globals + 4;
    for (size_t i = 0; i < script->global_count; i++) {
        at = get_value(at + 4 + 1, script->globals[i].type, global_values[i]);
    }
}
