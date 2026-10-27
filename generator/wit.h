/* The interface's WIT rendering, as `stasis-gen wit` writes it, for the component model's tools to
 * read. It's one file, named after the interface's namespace and name in kebab-case, as
 * acme-dungeon.wit is for `acme:dungeon`.
 *
 * The interface is a WIT package of the same namespace, name and version, in which:
 *
 *   - a group is an interface, a record a record, and a kind of host object a resource with its
 *     methods;
 *   - a handle is `borrow<T>` in a parameter, and `T` in a result or a field;
 *   - a kind of script is a world, which imports the groups the kind uses and exports its required
 *     entry points;
 *   - an optional entry point is an interface of its own, `<kind>-<entry point>`, which a script's
 *     world exports if the script provides the entry point.
 *
 * Names are in kebab-case, with a `%` before each that's a keyword of WIT's. WIT compares names
 * without their `-`s, and takes a method named as its resource as the resource's name. Two names
 * that are the same once their `_`s are dropped are an error in the interface, then, in one scope
 * or among the records and objects that a kind of script's entry points take and return, as are a
 * method named as its kind of host object and a group named as an optional entry point's interface.
 *
 * The rendering renames two kinds of name instead, neither of which names anything a component
 * imports or exports: a method's parameter that WIT takes as `self`, beside the handle that WIT
 * calls `self`, becomes `<name>-param`, and a type named as an optional entry point is, in the
 * entry point's interface, used as `<name>-type`. When that name is taken already, the suffix is
 * added again. */

#ifndef STASIS_GENERATOR_WIT_H
#define STASIS_GENERATOR_WIT_H

#include "arena.h"
#include "diagnostics.h"
#include "files.h"
#include "interface.h"

/* Reports every two names that WIT would take as the same where it must tell them apart, but for
 * duplicates, which reading the interface reports already. */
void check_wit_names(const struct interface *interface, struct arena *arena,
                     struct diagnostics *diagnostics);

/* Writes the WIT rendering of `interface`, which must have been read with no problems and have no
 * WIT names that clash, as `file`, which names the interface file it comes from as `input`. */
void generate_wit(const struct interface *interface, const char *input, struct arena *arena,
                  struct output_file *file);

#endif
