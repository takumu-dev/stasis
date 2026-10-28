#include "allocator.h"

#include <stddef.h>
#include <stdlib.h>

void *stasis_allocate(size_t size) { return malloc(size == 0 ? 1 : size); }

void *stasis_allocate_zeroed(size_t count, size_t size) {
    return calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
}

void stasis_deallocate(void *pointer) { free(pointer); }
