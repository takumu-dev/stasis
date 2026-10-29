/* The format of a snapshot, which is Stasis's own, the same on every backend, and tied to one
 * script. Every field is little-endian, and a snapshot is, in order:
 *
 *     magic       8 bytes: a NUL, "stasis" and a NUL
 *     version     u32, the format's version, 1
 *     identity    32 bytes, the script's identity
 *     memory      u64, the length of the script's memory in bytes, a whole number of pages
 *     globals     u32, the number of mutable globals the script defines, and then for each, in
 *                 increasing order of index: its index as a u32, its type as the byte that the
 *                 binary format encodes it as, and its value, as a u32 for an i32 or an f32, and a
 *                 u64 for an i64 or an f64
 *     handles     u64, the value of the next handle the instance would be given, which is 2^32 once
 *                 it has been given every handle it can have, and u32, the number of its live
 *                 handles, and then for each, in increasing order of value: its value as a u32, and
 *                 its kind's qualified name, as a u32 that counts its bytes and the bytes
 *     memory      the memory's bytes, in WebAssembly's order
 *
 * Nothing about the writer goes in, so the same state gives the same bytes everywhere. A snapshot
 * is untrusted input, so the reader checks every field before anything uses it. */

#ifndef STASIS_SNAPSHOT_FORMAT_H
#define STASIS_SNAPSHOT_FORMAT_H

#include <stasis.h>
#include <stasis_interface.h>

#include "handles.h"
#include "message.h"

#include <stddef.h>
#include <stdint.h>

/* A mutable global of a snapshottable script, as its snapshots hold it: its index among the
 * script's globals, and its type, as the binary format encodes it. */
struct snapshot_global {
    uint32_t index;
    uint8_t type;
};

/* What a snapshot of a script's instances must agree with: the script's identity, the limits its
 * memory grows within, in pages, its mutable globals in increasing order of index, and the kinds of
 * host object of its interface, which the handles name. */
struct snapshot_script {
    const uint8_t *identity;
    uint32_t minimum_pages;
    uint32_t maximum_pages;
    const struct snapshot_global *globals;
    size_t global_count;
    const stasis_interface *interface_data;
};

/* An instance's state, as a snapshot holds it: its memory, the value of each of its script's
 * mutable globals, as 4 or 8 bytes by type, in the host's byte order, and its handles. */
struct snapshot_state {
    const unsigned char *memory;
    uint64_t memory_size;
    void *const *global_values;
    const struct handle_table *handles;
};

/* The size in bytes of a snapshot of `state`, an instance of `script`. */
size_t stasis_snapshot_size(const struct snapshot_script *script,
                            const struct snapshot_state *state);

/* Writes a snapshot of `state`, an instance of `script`, into `buffer`, which holds the size that
 * stasis_snapshot_size gives. */
void stasis_snapshot_write(const struct snapshot_script *script, const struct snapshot_state *state,
                           unsigned char *buffer);

/* A snapshot that has passed the reader's checks: where its globals, its live handles and its
 * memory are in its bytes, and the next handle value. */
struct snapshot_reading {
    const unsigned char *globals;
    uint64_t next_handle;
    uint32_t handle_count;
    const unsigned char *handles;
    const unsigned char *memory;
    uint64_t memory_size;
};

/* Checks the `size` bytes at `bytes` as a snapshot of `script`, and fills in `reading`. Returns
 * STASIS_OK, or STASIS_WRONG_SCRIPT, STASIS_SNAPSHOT_TOO_NEW or STASIS_INVALID_SNAPSHOT, having
 * added a line to `message` that names the check that failed. */
stasis_status stasis_snapshot_read(struct snapshot_reading *reading,
                                   const struct snapshot_script *script, const unsigned char *bytes,
                                   size_t size, struct message *message);

/* One of the live handles of a snapshot: its value, and its kind's id. */
struct snapshot_handle {
    uint32_t value;
    uint32_t kind;
};

/* Fills in `handles` with the live handles of a snapshot of `script` that passed the reader's
 * checks, in increasing order of value, which hold its handle count. */
void stasis_snapshot_handles(const struct snapshot_reading *reading,
                             const struct snapshot_script *script, struct snapshot_handle *handles);

/* Writes the values that a snapshot of `script` that passed the reader's checks holds for the
 * script's mutable globals, each as 4 or 8 bytes by type, in the host's byte order, at its place in
 * `global_values`. */
void stasis_snapshot_globals(const struct snapshot_reading *reading,
                             const struct snapshot_script *script, void *const *global_values);

#endif
