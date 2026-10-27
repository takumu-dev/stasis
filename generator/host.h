/* The host's side of an interface, as `stasis-gen host` writes it: a header and a source, which
 * hold the interface's descriptors, a thunk for each host function, the prototypes of the host
 * functions that the host defines, and a typed caller for each entry point.
 *
 * Both are named after the prefix without its last `_`, as acme_dungeon.h and acme_dungeon.c are
 * for the prefix `acme_dungeon_`. The header is C99 that also compiles as C++11 or later, and the
 * source is C99. */

#ifndef STASIS_GENERATOR_HOST_H
#define STASIS_GENERATOR_HOST_H

#include "arena.h"
#include "files.h"
#include "interface.h"

/* The number of files in the host's side. */
enum { HOST_FILE_COUNT = 2 };

/* Writes the host's side of `interface`, which must have been read with no problems and have no C
 * names that clash, into `files`: the header, then the source. Every C name starts with `prefix`,
 * and the files name the interface file they come from as `input`. */
void generate_host(const struct interface *interface, const char *prefix, const char *input,
                   struct arena *arena, struct output_file files[HOST_FILE_COUNT]);

#endif
