/* Stasis's calls into a running script instance: the entry point or stasis_initialize that a call
 * runs, and the canonical ABI's functions that Stasis calls as values cross, cabi_realloc, to
 * allocate in the script's memory, and an entry point's cabi_post_*, to free its results.
 *
 * While Stasis is inside cabi_realloc or a cabi_post_*, the script may call no import, as the
 * canonical ABI's may_leave says. So Stasis's own calls nest at most one level deep: into
 * cabi_realloc, from a host function whose result holds a string.
 *
 * When a call traps, the instance's trap report is filled in at once, before any call it's nested
 * in unwinds, so that the report keeps the kind, the message and the frames of the first trap. */

#ifndef STASIS_CALLS_H
#define STASIS_CALLS_H

#include <stasis.h>
#include <stasis_interface.h>

#include "descriptors.h"
#include "layout.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wasm_export.h>

/* A value that Stasis puts into a script's memory, as messages name it: argument `position`, from
 * 1, of `entry_point`, or all its arguments when `position` is 0, or when `entry_point` is null,
 * the result of `host_function`. A value in a record names the record's fields that it's in by
 * their `path`, as a layout's leaf does, which is null for a value in no record. */
struct placed_value {
    const stasis_entry_point *entry_point;
    size_t position;
    const stasis_host_function *host_function;
    const char *path;
};

/* Calls `function` in the running instance with the core arguments given, and its results. Returns
 * whether it returned. When it trapped, the trap report is filled in, unless an earlier trap of the
 * same call into the instance filled it in already. */
bool stasis_invoke(stasis_instance *instance, wasm_function_inst_t function, uint32_t result_count,
                   wasm_val_t *results, uint32_t argument_count, wasm_val_t *arguments);

/* Ends the call that the running instance is in with the trap of Stasis's own that it has raised:
 * WAMR unwinds the call once the host function it's in returns, and the trap report reads the
 * raised trap. */
void stasis_end_call(stasis_instance *instance);

/* Fills in the trap report of an instance whose call has trapped, unless it's filled in already:
 * from the trap of Stasis's own that the call has raised, if any, and otherwise from WAMR's. */
void stasis_keep_trap(stasis_instance *instance);

/* Allocates `size` bytes aligned to `alignment` in the running instance's memory for `value`, with
 * the script's cabi_realloc, as the canonical ABI does, and sets `address` to where they are.
 * Returns false when the call is to end: cabi_realloc trapped, or returned null, which raises
 * allocation_failed, or an address that's misaligned, or where the bytes run past the end of
 * memory, which raises boundary_violation. */
bool stasis_reallocate(stasis_instance *instance, const struct placed_value *value,
                       uint32_t alignment, uint32_t size, uint32_t *address);

/* Puts `string` into the running instance's memory for `value`, as bytes it allocates with
 * stasis_reallocate, and sets `address` to where they are. Returns false when the call is to end,
 * as stasis_reallocate does. */
bool stasis_place_string(stasis_instance *instance, const struct placed_value *value,
                         stasis_string string, uint32_t *address);

/* Sets `handle` to the running instance's handle for `object`, of the kind `kind`, which is `value`
 * as messages name it, issuing one into room reserved for it if the instance has none. Returns
 * false, having raised handles_exhausted, if the instance has been given every handle it can
 * have. */
bool stasis_lower_handle(stasis_instance *instance, const struct placed_value *value, uint32_t kind,
                         void *object, uint32_t *handle);

/* Stores the values that `layout` lays out, from the C struct at `source`, into the running
 * instance's memory, as the tuple at `address`, which is in memory and aligned. Each string goes
 * into the memory as stasis_place_string puts it, each host object as stasis_lower_handle lowers
 * it, and messages name either as part of `owner`, with its leaf's position and path. Returns false
 * when the call is to end, as stasis_place_string and stasis_lower_handle do. */
bool stasis_store_values(stasis_instance *instance, const struct layout *layout, const void *source,
                         uint32_t address, const struct placed_value *owner);

/* Calls `function`, the cabi_post_* of `entry_point`, in the running instance, with the core
 * results the entry point returned. Returns whether it returned. */
bool stasis_post_return(stasis_instance *instance, const stasis_entry_point *entry_point,
                        wasm_function_inst_t function, uint32_t result_count, wasm_val_t *results);

/* Whether the running instance may call an import now: it may unless Stasis is inside its
 * cabi_realloc or a cabi_post_*. */
bool stasis_may_leave(const stasis_instance *instance);

/* Raises the boundary violation of a script that called the import `import` when it may not. */
void stasis_refuse_leaving(stasis_instance *instance, const struct host_function_name *import);

#endif
