#include "diagnostics.h"

#include "arena.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* A note, which points at a place that bears on an error. */
struct note {
    struct note *next;
    struct position position;
    const char *message;
};

/* An error, with its notes. */
struct diagnostic {
    struct diagnostic *next;
    struct position position;
    const char *message;
    struct note *notes;
    struct note *last_note;
    /* How many errors were reported before it. */
    size_t sequence;
};

void report_error(struct diagnostics *diagnostics, struct position position, const char *format,
                  ...) {
    struct diagnostic *error = arena_allocate(diagnostics->arena, sizeof *error);
    error->position = position;
    va_list arguments;
    va_start(arguments, format);
    error->message = arena_vformat(diagnostics->arena, format, arguments);
    va_end(arguments);
    error->sequence = diagnostics->count++;
    if (diagnostics->last == NULL) {
        diagnostics->first = error;
    } else {
        diagnostics->last->next = error;
    }
    diagnostics->last = error;
}

void report_note(struct diagnostics *diagnostics, struct position position, const char *format,
                 ...) {
    struct note *note = arena_allocate(diagnostics->arena, sizeof *note);
    note->position = position;
    va_list arguments;
    va_start(arguments, format);
    note->message = arena_vformat(diagnostics->arena, format, arguments);
    va_end(arguments);
    struct diagnostic *error = diagnostics->last;
    if (error->last_note == NULL) {
        error->notes = note;
    } else {
        error->last_note->next = note;
    }
    error->last_note = note;
}

int compare_positions(struct position a, struct position b) {
    if (a.line != b.line) {
        return a.line < b.line ? -1 : 1;
    }
    if (a.column != b.column) {
        return a.column < b.column ? -1 : 1;
    }
    return 0;
}

/* Orders errors by where they are, and errors in the same place in the order they were found. */
static int compare_errors(const void *a, const void *b) {
    const struct diagnostic *first = *(const struct diagnostic *const *)a;
    const struct diagnostic *second = *(const struct diagnostic *const *)b;
    const int order = compare_positions(first->position, second->position);
    if (order != 0) {
        return order;
    }
    return first->sequence < second->sequence ? -1 : 1;
}

void print_diagnostics(const struct diagnostics *diagnostics, const char *path, FILE *stream) {
    if (diagnostics->count == 0) {
        return;
    }
    const struct diagnostic **errors = (const struct diagnostic **)arena_allocate(
        diagnostics->arena, diagnostics->count * sizeof *errors);
    size_t count = 0;
    for (const struct diagnostic *error = diagnostics->first; error != NULL; error = error->next) {
        errors[count++] = error;
    }
    qsort((void *)errors, count, sizeof *errors, compare_errors);
    for (size_t i = 0; i < count; i++) {
        fprintf(stream, "%s:%zu:%zu: error: %s\n", path, errors[i]->position.line,
                errors[i]->position.column, errors[i]->message);
        for (const struct note *note = errors[i]->notes; note != NULL; note = note->next) {
            fprintf(stream, "%s:%zu:%zu: note: %s\n", path, note->position.line,
                    note->position.column, note->message);
        }
    }
}
