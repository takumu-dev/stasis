#include "wamr_hooks.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wasm_export.h>

/* WAMR's host-allocator functions, as it calls them when built with WASM_MEM_ALLOC_WITH_USAGE and
 * WASM_MEM_ALLOC_WITH_USER_DATA.
 *
 * Linear memory has to read as zeros wherever a script hasn't written it, both when an instance is
 * created and when its memory grows, but WAMR zeroes neither what it allocates for it nor what it
 * reallocates it to, and doesn't say how large a block was before it grows. So each block of linear
 * memory starts with a header that holds its size, which keeps the memory after it as aligned as
 * the C library's allocations are, and is zeroed wherever it's new. */

/* The size of the header before each block of linear memory. */
enum { MEMORY_HEADER_SIZE = 16 };

/* The size of the linear memory that starts after the header at `block`. */
static size_t memory_size(const unsigned char *block) {
    size_t size = 0;
    memcpy(&size, block, sizeof size);
    return size;
}

/* Reallocates the block of linear memory at `block`, or allocates one if it's null, so that it
 * holds `size` bytes, any of them new zeroed. Returns the memory after the header, or null. */
static void *resize_memory(unsigned char *block, size_t size) {
    const size_t old_size = block == NULL ? 0 : memory_size(block);
    unsigned char *resized = realloc(block, MEMORY_HEADER_SIZE + size);
    if (resized == NULL) {
        return NULL;
    }
    if (size > old_size) {
        memset(resized + MEMORY_HEADER_SIZE + old_size, 0, size - old_size);
    }
    memcpy(resized, &size, sizeof size);
    return resized + MEMORY_HEADER_SIZE;
}

/* The block of linear memory whose memory starts at `memory`, or null if it's null. */
static unsigned char *memory_block(void *memory) {
    return memory == NULL ? NULL : (unsigned char *)memory - MEMORY_HEADER_SIZE;
}

static void *stasis_wamr_malloc(mem_alloc_usage_t usage, void *user_data, unsigned int size) {
    (void)user_data;
    if (usage == Alloc_For_LinearMemory) {
        return resize_memory(NULL, size);
    }
    return malloc(size);
}

static void *stasis_wamr_realloc(mem_alloc_usage_t usage, bool full_size_mapped, void *user_data,
                                 void *pointer, unsigned int size) {
    (void)full_size_mapped;
    (void)user_data;
    if (usage == Alloc_For_LinearMemory) {
        return resize_memory(memory_block(pointer), size);
    }
    return realloc(pointer, size);
}

static void stasis_wamr_free(mem_alloc_usage_t usage, void *user_data, void *pointer) {
    (void)user_data;
    if (usage == Alloc_For_LinearMemory) {
        free(memory_block(pointer));
        return;
    }
    free(pointer);
}

/* WAMR takes its allocator's functions as object pointers, which ISO C can't convert a function
 * pointer to, so each is read back through a union. */
_Static_assert(sizeof(void *) == sizeof(void (*)(void)),
               "A function pointer is the size of an object pointer.");

void stasis_wamr_init_args(RuntimeInitArgs *args) {
    union {
        void *(*function)(mem_alloc_usage_t, void *, unsigned int);
        void *object;
    } malloc_function = {stasis_wamr_malloc};

    union {
        void *(*function)(mem_alloc_usage_t, bool, void *, void *, unsigned int);
        void *object;
    } realloc_function = {stasis_wamr_realloc};

    union {
        void (*function)(mem_alloc_usage_t, void *, void *);
        void *object;
    } free_function = {stasis_wamr_free};

    memset(args, 0, sizeof *args);
    args->mem_alloc_type = Alloc_With_Allocator;
    args->mem_alloc_option.allocator.malloc_func = malloc_function.object;
    args->mem_alloc_option.allocator.realloc_func = realloc_function.object;
    args->mem_alloc_option.allocator.free_func = free_function.object;
}

void stasis_wamr_log(uint32_t level, const char *file, int line, const char *format, ...) {
    (void)level;
    (void)file;
    (void)line;
    (void)format;
}

int stasis_wamr_vprintf(const char *format, va_list args) {
    (void)format;
    (void)args;
    return 0;
}
