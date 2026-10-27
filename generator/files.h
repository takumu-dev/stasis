/* Reading an interface file, and writing what stasis-gen generates. */

#ifndef STASIS_GENERATOR_FILES_H
#define STASIS_GENERATOR_FILES_H

#include <stdbool.h>
#include <stddef.h>

/* A file that a command writes: its name, and what it holds. */
struct output_file {
    const char *name;
    const char *contents;
    size_t size;
};

/* Reads a whole file, as a string the caller frees. It sets `size` to the file's size, which
 * doesn't count the terminating null. Returns null if the file can't be read, leaving errno to say
 * why. */
char *read_file(const char *path, size_t *size);

/* Writes the `size` bytes of `contents` as the file at `path`, unless it holds exactly those bytes
 * already. A file left unchanged keeps its modification time, so a build doesn't redo what depends
 * on it. Returns false if the file can't be written, leaving errno to say why. */
bool update_file(const char *path, const char *contents, size_t size);

#endif
