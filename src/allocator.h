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

/* Counts an allocation that Stasis or WAMR makes, reallocations included, in the test-only library,
 * which builds the core with STASIS_TESTING. The tests read the count of the calling thread's
 * through stasis_testing_allocations. In the library itself, it's nothing. */
#ifdef STASIS_TESTING
void stasis_count_allocation(void);
size_t stasis_testing_allocations(void);
#define STASIS_COUNT_ALLOCATION() stasis_count_allocation()
#else
#define STASIS_COUNT_ALLOCATION() ((void)0)
#endif

#endif
