/* Compiles the host side of escaping.stasis, as C, where the lowercase macros it escapes are
 * defined as a host's headers may define them: errno and the standard streams by the C library,
 * bool, true and false by <stdbool.h>, linux and unix by GCC and Clang in their GNU modes, and
 * small, near and far by Windows' headers. */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#define linux 1
#define unix 1
#define small char
#define near
#define far

#include "test_escaping.c"
