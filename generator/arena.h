/* An arena: memory that stasis-gen allocates piece by piece and frees all at once.
 *
 * Allocating never fails. If memory runs out, stasis-gen says so and exits with 2. */

#ifndef STASIS_GENERATOR_ARENA_H
#define STASIS_GENERATOR_ARENA_H

#include <stdarg.h>
#include <stddef.h>

/* Lets GCC and Clang check a printf-style format against its arguments. */
#ifdef __GNUC__
#define PRINTF_FORMAT(string, first) __attribute__((format(printf, string, first)))
#else
#define PRINTF_FORMAT(string, first)
#endif

/* An arena, which starts out zeroed and empty. */
struct arena {
    struct arena_block *blocks;
};

/* Allocates `size` bytes, zeroed and aligned for any type. */
void *arena_allocate(struct arena *arena, size_t size);

/* Makes room for one more item in `items`, an array of `count` items of `size` bytes that has room
 * for `*capacity`. Returns the array, which is a larger copy, with `*capacity` updated, if it was
 * full. */
void *arena_grow(struct arena *arena, void *items, size_t count, size_t *capacity, size_t size);

/* Copies `length` bytes of `text`, adding a null after them. */
char *arena_copy(struct arena *arena, const char *text, size_t length);

/* Formats a string as vsnprintf does. */
char *arena_vformat(struct arena *arena, const char *format, va_list arguments) PRINTF_FORMAT(2, 0);

/* Formats a string as snprintf does. */
char *arena_format(struct arena *arena, const char *format, ...) PRINTF_FORMAT(2, 3);

/* Frees everything the arena holds, and empties it. */
void arena_free(struct arena *arena);

#endif
