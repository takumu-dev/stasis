/* How Stasis initialises WAMR, with an allocator of its own, and the functions WAMR is built to
 * send its logging and printing to. Stasis never writes to the host's output, so both are
 * discarded. */

#ifndef STASIS_WAMR_HOOKS_H
#define STASIS_WAMR_HOOKS_H

#include <stdarg.h>
#include <stdint.h>
#include <wasm_export.h>

/* Sets `args` to how Stasis initialises WAMR: in its host-allocator mode, through Stasis's own
 * functions, which forward to the C library's and zero linear memory, and with no host
 * functions. */
void stasis_wamr_init_args(RuntimeInitArgs *args);

/* WAMR's logging, which Stasis builds it to send here, at any level. It's discarded. */
void stasis_wamr_log(uint32_t level, const char *file, int line, const char *format, ...);

/* WAMR's printing, such as the call stack it prints when a script traps, which the library builds
 * it to send here. It's discarded, and the result is the 0 characters printed. */
int stasis_wamr_vprintf(const char *format, va_list args);

#endif
