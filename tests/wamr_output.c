/* Where WAMR's printing goes in the test-only library: the test's output, where the library itself
 * discards it. */

#include <stdarg.h>
#include <stdio.h>

/* Marks a function that takes a printf format and a va_list, as vprintf does. */
#ifdef __GNUC__
#define STASIS_VPRINTF_LIKE __attribute__((format(printf, 1, 0)))
#else
#define STASIS_VPRINTF_LIKE
#endif

/* NOLINTNEXTLINE(misc-use-internal-linkage): WAMR calls it, as BH_VPRINTF names it */
STASIS_VPRINTF_LIKE int stasis_testing_vprintf(const char *format, va_list args);

int stasis_testing_vprintf(const char *format, va_list args) { return vprintf(format, args); }
