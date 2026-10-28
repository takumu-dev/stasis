/* How values cross between the C structs that hold them on the host's side and the core values
 * that a script's functions take and return, or the script's memory, as the canonical ABI lowers
 * and lifts them. So far only scalars cross. */

#ifndef STASIS_BOUNDARY_H
#define STASIS_BOUNDARY_H

#include <stasis_interface.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wasm_export.h>

/* Whether a value is a scalar: an integer, a float or a bool. */
bool stasis_is_scalar(const stasis_value *value);

/* Lowers the scalar at `value`'s offset in `source` into a core value: an integer of 32 bits or
 * fewer into an i32, extended by its sign if it has one, a bool into 0 or 1, and every other
 * scalar into the core type of its own size, bit for bit. */
void stasis_lower_scalar(const stasis_value *value, const void *source, wasm_val_t *core);

/* Lifts a core value into the scalar at `value`'s offset in `destination`. An i32 is narrowed to an
 * integer of fewer bits by keeping its low bits, and becomes a bool that's true unless it's 0. */
void stasis_lift_scalar(const stasis_value *value, const wasm_val_t *core, void *destination);

/* The two above, for a core value in a slot of WAMR's raw calling convention, in which a host
 * function takes each core value in 64 bits of its own, starting at the slot's first byte, and
 * returns its result in the first slot. */
void stasis_lift_raw_scalar(const stasis_value *value, const uint64_t *slot, void *destination);
void stasis_lower_raw_scalar(const stasis_value *value, const void *source, uint64_t *slot);

/* The number of bytes a scalar takes in a script's memory, which is its alignment there too, as the
 * canonical ABI lays it out. */
size_t stasis_scalar_memory_size(const stasis_value *value);

/* Lifts the scalar that the canonical ABI stored in a script's memory at `at`, little-endian, into
 * the scalar at `value`'s offset in `destination`. A bool is true unless its byte is 0. */
void stasis_load_scalar(const stasis_value *value, const unsigned char *at, void *destination);

#endif
