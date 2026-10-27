/* Text that stasis-gen builds up piece by piece, such as a file it writes, and the lists of words
 * that it looks names up in. */

#ifndef STASIS_GENERATOR_TEXT_H
#define STASIS_GENERATOR_TEXT_H

#include "arena.h"

#include <stdbool.h>
#include <stddef.h>

/* The column that generated lines stay within, where they can. */
enum { LINE_LIMIT = 100 };

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

/* Adds a paragraph in lines that each start with `start` and stay within the limit where they
 * can: as many of its words as fit, each after a space, then a newline. */
void text_fill(struct text *text, const char *start, const char *paragraph);

/* A list of strings, such as the parameters of a function being declared, which starts out zeroed
 * and empty. */
struct strings {
    const char **items;
    size_t count;
    size_t capacity;
};

/* Adds a string to a list, which grows in `arena`. */
void strings_add(struct strings *strings, struct arena *arena, const char *string);

/* The length of the strings in a list, joined by ", ". */
size_t joined_length(const struct strings *strings);

/* Adds the strings in a list, joined by ", ". */
void text_append_joined(struct text *text, const struct strings *strings);

/* Whether `name` is one of `words`, which are each between spaces. */
bool is_among(const char *name, const char *words);

#endif
