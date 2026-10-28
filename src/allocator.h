/* Where Stasis's own allocations go: every object and buffer of the core's comes from here, and
 * goes back here. Each forwards to the C library's allocator. */

#ifndef STASIS_ALLOCATOR_H
#define STASIS_ALLOCATOR_H

#include <stddef.h>

/* Allocates `size` bytes, or returns null. */
void *stasis_allocate(size_t size);

/* Allocates `count` items of `size` bytes each, zeroed, or returns null, as calloc does. */
void *stasis_allocate_zeroed(size_t count, size_t size);

/* Frees what stasis_allocate or stasis_allocate_zeroed allocated. `pointer` may be null. */
void stasis_deallocate(void *pointer);

#endif
