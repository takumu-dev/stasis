#include "wamr_hooks.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wasm_export.h>

/* WAMR's host-allocator functions, as it calls them when built with WASM_MEM_ALLOC_WITH_USAGE and
 * WASM_MEM_ALLOC_WITH_USER_DATA. */

static void *stasis_wamr_malloc(mem_alloc_usage_t usage, void *user_data, unsigned int size) {
    (void)usage;
    (void)user_data;
    return malloc(size);
}

static void *stasis_wamr_realloc(mem_alloc_usage_t usage, bool full_size_mapped, void *user_data,
                                 void *pointer, unsigned int size) {
    (void)usage;
    (void)full_size_mapped;
    (void)user_data;
    return realloc(pointer, size);
}

static void stasis_wamr_free(mem_alloc_usage_t usage, void *user_data, void *pointer) {
    (void)usage;
    (void)user_data;
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
