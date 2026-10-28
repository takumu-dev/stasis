/* How values cross between the C structs that hold them on the host's side and the core values
 * that a script's functions take and return, or the script's memory, as the canonical ABI lowers
 * and lifts them. So far only scalars and strings cross. */

#ifndef STASIS_BOUNDARY_H
#define STASIS_BOUNDARY_H

#include <stasis.h>
#include <stasis_interface.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wasm_export.h>

/* The most bytes a string may have, as the canonical ABI has it. */
enum { STASIS_MAX_STRING_SIZE = 0x7FFFFFFF };

/* Whether a value is a scalar: an integer, a float or a bool. */
bool stasis_is_scalar(const stasis_value *value);

/* Whether a value is of a type that can cross yet: a scalar or a string. */
bool stasis_can_cross(const stasis_value *value);

/* The number of core values that a value that can cross flattens to: one for a scalar, and two for
 * a string, its address in the script's memory and its size. */
size_t stasis_flat_count(const stasis_value *value);

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

/* The number of bytes that a value that can cross takes in a script's memory, as the canonical ABI
 * lays it out, and its alignment there. A scalar is as aligned as it's large, and a string is its
 * address and its size, 4 bytes each. */
size_t stasis_memory_size(const stasis_value *value);
size_t stasis_memory_alignment(const stasis_value *value);

/* Loads the 32-bit integer stored little-endian at `at`, in a script's memory, or stores one
 * there. */
uint32_t stasis_load_u32(const unsigned char *at);
void stasis_store_u32(unsigned char *at, uint32_t value);

/* A script instance's memory, as it is now. It moves whenever the script's code runs, which can
 * grow it, so a pointer into it is taken afresh after anything that runs the script's code. */
struct script_memory {
    unsigned char *base;
    uint64_t size;
};

/* The memory of the instance `module_instance`, which is empty if it has none. */
struct script_memory stasis_script_memory(wasm_module_inst_t module_instance);

/* The number of bytes at the start of the `size` at `bytes` that are whole characters of UTF-8, as
 * Unicode defines it, without overlong encodings, surrogates, or anything past U+10FFFF. It's
 * `size` when they all are, and otherwise the offset of the first character that isn't. */
size_t stasis_utf8_length(const unsigned char *bytes, size_t size);

/* What, if anything, keeps a string that the host passes from crossing. */
enum string_fault {
    STRING_CROSSES,
    STRING_DATA_NULL,
    STRING_TOO_LARGE,
    STRING_NOT_UTF8,
};

/* Checks a string that the host passes: its data has to be there unless it's empty, it can be no
 * larger than a string may be, and it has to be UTF-8. When it isn't UTF-8, sets `valid` to the
 * offset of its first character that isn't. */
enum string_fault stasis_check_host_string(stasis_string string, size_t *valid);

/* Lifts the scalar that the canonical ABI stored in a script's memory at `at`, little-endian, into
 * the scalar at `value`'s offset in `destination`. A bool is true unless its byte is 0. */
void stasis_load_scalar(const stasis_value *value, const unsigned char *at, void *destination);

#endif
