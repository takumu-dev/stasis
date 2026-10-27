#include "files.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    size_t used = 0;
    size_t capacity = 1 << 12;
    char *text = malloc(capacity);
    while (text != NULL) {
        used += fread(text + used, 1, capacity - used - 1, file);
        if (used < capacity - 1) {
            break;
        }
        capacity *= 2;
        char *grown = realloc(text, capacity);
        if (grown == NULL) {
            free(text);
        }
        text = grown;
    }
    if (text != NULL && ferror(file) != 0) {
        free(text);
        text = NULL;
    }
    const int error = errno;
    fclose(file);
    errno = error;
    if (text != NULL) {
        text[used] = '\0';
        *size = used;
    }
    return text;
}

/* Whether the file at `path` holds exactly the `size` bytes of `contents`. */
static bool holds(const char *path, const char *contents, size_t size) {
    size_t existing_size = 0;
    char *existing = read_file(path, &existing_size);
    if (existing == NULL) {
        return false;
    }
    bool same = false;
    if (existing_size == size) {
        same = memcmp(existing, contents, size) == 0;
    }
    free(existing);
    return same;
}

bool update_file(const char *path, const char *contents, size_t size) {
    if (holds(path, contents, size)) {
        return true;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const bool written = fwrite(contents, 1, size, file) == size;
    const int error = errno;
    const bool closed = fclose(file) == 0;
    if (!written) {
        errno = error;
        return false;
    }
    return closed;
}
