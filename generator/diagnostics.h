/* The errors found in an interface file, each with any notes that point at other places in it.
 *
 * They're printed in order of where they are, as
 *
 *     <path>:<line>:<column>: error: <message>
 *     <path>:<line>:<column>: note: <message>
 *
 * with each error's notes after it. There are no warnings. */

#ifndef STASIS_GENERATOR_DIAGNOSTICS_H
#define STASIS_GENERATOR_DIAGNOSTICS_H

#include "arena.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* A place in an interface file: a line, and a column that counts bytes, both from 1. */
struct position {
    size_t line;
    size_t column;
};

/* Orders two places in a file: negative if `a` comes first, positive if `b` does, and 0 if they're
 * the same place. */
int compare_positions(struct position a, struct position b);

/* The errors found so far, which start out zeroed and empty. Their messages live in `arena`. */
struct diagnostics {
    struct arena *arena;
    struct diagnostic *first;
    struct diagnostic *last;
    size_t count;
};

/* Reports an error at `position`. */
void report_error(struct diagnostics *diagnostics, struct position position, const char *format,
                  ...) PRINTF_FORMAT(3, 4);

/* Adds a note at `position` to the error reported last. */
void report_note(struct diagnostics *diagnostics, struct position position, const char *format, ...)
    PRINTF_FORMAT(3, 4);

/* Prints every error and its notes to `stream`, in order of where they are in the file at
 * `path`. */
void print_diagnostics(const struct diagnostics *diagnostics, const char *path, FILE *stream);

#endif
