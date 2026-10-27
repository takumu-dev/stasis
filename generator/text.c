#include "text.h"

#include "arena.h"

#include <stdarg.h>
#include <stdbool.h>
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

void text_fill(struct text *text, const char *start, const char *paragraph) {
    const size_t start_length = strlen(start);
    size_t column = start_length;
    text_append(text, start);
    const char *word = paragraph;
    while (*word != '\0') {
        const char *end = strchr(word, ' ');
        const size_t length = end == NULL ? strlen(word) : (size_t)(end - word);
        if (column > start_length && column + 1 + length > LINE_LIMIT) {
            text_format(text, "\n%s", start);
            column = start_length;
        }
        text_append(text, " ");
        text_append_length(text, word, length);
        column += 1 + length;
        word += length;
        while (*word == ' ') {
            word++;
        }
    }
    text_append(text, "\n");
}

void strings_add(struct strings *strings, struct arena *arena, const char *string) {
    strings->items = (const char **)arena_grow(arena, (void *)strings->items, strings->count,
                                               &strings->capacity, sizeof *strings->items);
    strings->items[strings->count++] = string;
}

size_t joined_length(const struct strings *strings) {
    size_t length = 0;
    for (size_t i = 0; i < strings->count; i++) {
        length += strlen(strings->items[i]) + (i > 0 ? 2 : 0);
    }
    return length;
}

void text_append_joined(struct text *text, const struct strings *strings) {
    for (size_t i = 0; i < strings->count; i++) {
        if (i > 0) {
            text_append(text, ", ");
        }
        text_append(text, strings->items[i]);
    }
}

bool is_among(const char *name, const char *words) {
    const size_t length = strlen(name);
    for (const char *found = strstr(words, name); found != NULL; found = strstr(found + 1, name)) {
        if (found[-1] == ' ' && found[length] == ' ') {
            return true;
        }
    }
    return false;
}
