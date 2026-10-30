/* The C bindings of an interface, as `stasis-gen c` writes them: a header and a source for each
 * kind of script, which a C script built for wasm32-wasip1 includes and is built with. They include
 * only the C library's headers, so a script needs nothing else of Stasis's.
 *
 * The header declares `<prefix>string`, a struct for each record, with a `_free` helper for each
 * that holds a string, and a handle struct for each kind of host object. It declares a function
 * for each host function and method of the groups the kind of script uses, through which the
 * script calls it, and each entry point, which the script defines: a required one by its name, and
 * an optional one through its macro. The macro expands to the entry point's export, and the
 * cabi_post_* that frees its result if that holds a string, before the start of the function's
 * definition, so a script exports an optional entry point exactly when it defines it. The header
 * refuses to compile for any target but wasm32-wasip1 without threads.
 *
 * The source defines what the header declares besides the entry points, and what Stasis calls:
 * stasis_initialize, which runs the script's constructors, each required entry point's export and
 * cabi_post_*, and cabi_realloc when Stasis needs it. It also puts the licence notices of
 * wasi-sdk's C library into a custom section, `notices`.
 *
 * Strings that a script passes to a host function stay its own, and those a host function returns
 * are the script's to free. An entry point's arguments are borrowed: its export frees the strings
 * in them once the script's function returns. What an entry point returns is copied, and its
 * cabi_post_* frees the copy, so the script's function can return a string literal.
 *
 * The files are named after the prefix and the kind of script, as acme_dungeon_npc.h and
 * acme_dungeon_npc.c are for the prefix `acme_dungeon_` and the kind of script `npc`. */

#ifndef STASIS_GENERATOR_C_BINDINGS_H
#define STASIS_GENERATOR_C_BINDINGS_H

#include "arena.h"
#include "files.h"
#include "interface.h"

#include <stddef.h>

/* The number of files in the C bindings of `interface`: two for each kind of script. */
size_t c_bindings_file_count(const struct interface *interface);

/* Writes the C bindings of `interface`, which must have been read with no problems and have no C
 * names that clash, into `files`, which has room for c_bindings_file_count() of them: each kind of
 * script's header, then its source. Every C name starts with `prefix`, and the files name the
 * interface file they come from as `input`. */
void generate_c_bindings(const struct interface *interface, const char *prefix, const char *input,
                         struct arena *arena, struct output_file *files);

#endif
