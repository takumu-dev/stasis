#include "allocator.h"

#include <stddef.h>
#include <stdlib.h>

void *stasis_allocate(size_t size) {
    STASIS_COUNT_ALLOCATION();
    return malloc(size == 0 ? 1 : size);
}

void *stasis_allocate_zeroed(size_t count, size_t size) {
    STASIS_COUNT_ALLOCATION();
    return calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
}

void stasis_deallocate(void *pointer) { free(pointer); }

#ifdef STASIS_TESTING

/* The number of allocations made on this thread, which only it reads. */
static _Thread_local size_t allocations;

void stasis_count_allocation(void) { allocations++; }

size_t stasis_testing_allocations(void) { return allocations; }

#endif
