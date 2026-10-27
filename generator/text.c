#include "text.h"

#include "arena.h"

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

void text_init(struct text *text, struct arena *arena) {
    text->arena = arena;
    text->capacity = 1 << 10;
    text->data = arena_allocate(arena, text->capacity);
    text->length = 0;
}

void text_append_length(struct text *text, const char *string, size_t length) {
    if (text->capacity - text->length <= length) {
        size_t capacity = 2 * text->capacity;
        while (capacity - text->length <= length) {
            capacity *= 2;
        }
        char *grown = arena_allocate(text->arena, capacity);
        memcpy(grown, text->data, text->length);
        text->data = grown;
        text->capacity = capacity;
    }
    memcpy(text->data + text->length, string, length);
    text->length += length;
    text->data[text->length] = '\0';
}

void text_append(struct text *text, const char *string) {
    text_append_length(text, string, strlen(string));
}

void text_format(struct text *text, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    text_append(text, arena_vformat(text->arena, format, arguments));
    va_end(arguments);
}
