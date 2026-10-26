/* Checks a parsed interface against the format's rules beyond its syntax, and resolves every type
 * it names.
 *
 * It checks that names are unique within their scope, that `use` and `uses` name what exists, that
 * groups don't use each other in a cycle and records don't contain themselves, that every type is
 * known, that the `stasis` namespace isn't used, and that there's exactly one kind of script. */

#ifndef STASIS_GENERATOR_CHECKER_H
#define STASIS_GENERATOR_CHECKER_H

#include "arena.h"
#include "diagnostics.h"
#include "interface.h"

/* Checks `interface`, reporting every problem with it to `diagnostics`. */
void check_interface(struct interface *interface, struct arena *arena,
                     struct diagnostics *diagnostics);

#endif
