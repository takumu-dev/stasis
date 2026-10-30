/* The licence notices of the C library that the pinned wasi-sdk links into C scripts, which the C
 * bindings put into every script: the text of wasi_libc_notices.txt, which the build compiles into
 * stasis-gen. A commit that moves the pinned wasi-sdk checks them against its sources. */

#ifndef STASIS_GENERATOR_NOTICES_H
#define STASIS_GENERATOR_NOTICES_H

#include <stddef.h>

/* The notices, as UTF-8 text in lines that each end in a newline. */
extern const unsigned char wasi_libc_notices[];

/* The number of bytes in the notices. */
extern const size_t wasi_libc_notices_size;

#endif
