/* Text that stasis-gen builds up piece by piece, such as a file it writes. */

#ifndef STASIS_GENERATOR_TEXT_H
#define STASIS_GENERATOR_TEXT_H

#include "arena.h"

#include <stddef.h>

/* Text, which always ends in a null, allocated in an arena. */
struct text {
    struct arena *arena;
    char *data;
    size_t length;
    size_t capacity;
};

/* Starts empty text, allocated in `arena`. */
void text_init(struct text *text, struct arena *arena);

/* Adds `length` bytes of `string`. */
void text_append_length(struct text *text, const char *string, size_t length);

/* Adds a string. */
void text_append(struct text *text, const char *string);

/* Adds a string formatted as snprintf does. */
void text_format(struct text *text, const char *format, ...) PRINTF_FORMAT(2, 3);

#endif
