#include "arena.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The size of a block, unless an allocation needs more. */
enum { BLOCK_SIZE = 1 << 16 };

/* The types that need the strictest alignment, which every allocation has. C's max_align_t would
 * say, but MSVC's C library lacks it. */
union alignment {
    long double number;
    long long integer;
    void *pointer;
    void (*function)(void);
};

#define ALIGNMENT _Alignof(union alignment)

/* A block of memory, from which allocations are taken in turn. Its bytes follow it, from
 * HEADER_SIZE bytes after its start. */
struct arena_block {
    struct arena_block *next;
    size_t size;
    size_t used;
};

enum { HEADER_SIZE = (sizeof(struct arena_block) + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT };

/* Ends stasis-gen when memory runs out, as nothing else can be done. */
_Noreturn static void out_of_memory(void) {
    fputs("stasis-gen: out of memory\n", stderr);
    fflush(stdout);
    _Exit(2);
}

void *arena_allocate(struct arena *arena, size_t size) {
    size = (size + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
    struct arena_block *block = arena->blocks;
    if (block == NULL || block->size - block->used < size) {
        const size_t block_size = size > BLOCK_SIZE ? size : BLOCK_SIZE;
        block = malloc(HEADER_SIZE + block_size);
        if (block == NULL) {
            out_of_memory();
        }
        block->next = arena->blocks;
        block->size = block_size;
        block->used = 0;
        arena->blocks = block;
    }
    unsigned char *allocation = (unsigned char *)block + HEADER_SIZE + block->used;
    block->used += size;
    memset(allocation, 0, size);
    return allocation;
}

void *arena_grow(struct arena *arena, void *items, size_t count, size_t *capacity, size_t size) {
    if (count < *capacity) {
        return items;
    }
    *capacity = *capacity == 0 ? 8 : 2 * *capacity;
    void *grown = arena_allocate(arena, *capacity * size);
    if (count > 0) {
        memcpy(grown, items, count * size);
    }
    return grown;
}

char *arena_copy(struct arena *arena, const char *text, size_t length) {
    char *copy = arena_allocate(arena, length + 1);
    memcpy(copy, text, length);
    return copy;
}

char *arena_vformat(struct arena *arena, const char *format, va_list arguments) {
    va_list again;
    va_copy(again, arguments);
    const int length = vsnprintf(NULL, 0, format, again);
    va_end(again);
    if (length < 0) {
        out_of_memory();
    }
    char *text = arena_allocate(arena, (size_t)length + 1);
    vsnprintf(text, (size_t)length + 1, format, arguments);
    return text;
}

char *arena_format(struct arena *arena, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char *text = arena_vformat(arena, format, arguments);
    va_end(arguments);
    return text;
}

void arena_free(struct arena *arena) {
    while (arena->blocks != NULL) {
        struct arena_block *next = arena->blocks->next;
        free(arena->blocks);
        arena->blocks = next;
    }
}
