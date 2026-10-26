/* Parses an interface file into an interface, whose types are left unresolved.
 *
 * The parser reports every syntax error and every name that breaks the rules for names. After an
 * error, it picks up again at the next declaration, or at the next item of a list. */

#ifndef STASIS_GENERATOR_PARSER_H
#define STASIS_GENERATOR_PARSER_H

#include "arena.h"
#include "diagnostics.h"
#include "interface.h"

#include <stddef.h>

/* Parses the `size` bytes of `source`, allocating the interface in `arena`. */
struct interface *parse_interface(const char *source, size_t size, struct arena *arena,
                                  struct diagnostics *diagnostics);

#endif
