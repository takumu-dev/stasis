/* How values cross between the C structs that hold them on the host's side and the core values
 * that a script's functions take and return, as the canonical ABI lowers and lifts them. So far
 * only scalars cross, each as one core value. */

#ifndef STASIS_BOUNDARY_H
#define STASIS_BOUNDARY_H

#include <stasis_interface.h>

#include <stdbool.h>
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

#endif
