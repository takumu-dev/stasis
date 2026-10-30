/* The names that an interface's declarations take in C, and the check that no two are the same.
 *
 * Every name that the host's side or the C bindings of an interface declare outside a function
 * starts with a prefix, which is `<namespace>_<name>_` unless the host chooses another:
 *
 *     <prefix><group>_<object>           a kind of host object's opaque type, or its handle struct
 *     <prefix><group>_<record>           a record's struct
 *     <prefix><group>_<record>_free      the C bindings' helper that frees a record's strings
 *     <prefix><group>_<function>         a host function
 *     <prefix><group>_<object>_<method>  a method
 *     <prefix><script>_<entry point>     an entry point, or its typed caller
 *     <prefix>interface                  the interface's descriptors
 *     <prefix>string                     the C bindings' string type
 *     <prefix>string_of                  the C bindings' helper that makes one
 *
 * Their macros are uppercase: on the host's side, each kind of host object's and entry point's
 * name with `_KIND` or `_ENTRY` after it, in the C bindings, each optional entry point's name, and
 * the include guards, of which the C bindings' ends in `_`. Everything else the generated code
 * declares ends in `_`, which no name in an interface can, or is a name that Stasis calls, such as
 * stasis_initialize and cabi_realloc, which no prefix gives, so it never meets any of these.
 *
 * Joining scopes with `_` can give two declarations the same name, as with `world.entity_find`
 * beside `world.entity.find`. That's an error in the interface, which every command reports.
 *
 * Parameters and fields aren't prefixed. Each that would meet a keyword, a common macro or a
 * parameter of the generator's own takes a `_` after it. */

#ifndef STASIS_GENERATOR_C_NAMES_H
#define STASIS_GENERATOR_C_NAMES_H

#include "arena.h"
#include "diagnostics.h"
#include "interface.h"

/* What stops `prefix` from starting C names, as words that follow it in a message, or null if
 * nothing does. A prefix is lowercase snake_case, with every word starting with a letter, and a
 * `_` after its last word. One that starts with `stasis_` is reserved for Stasis's own names. */
const char *prefix_problem(const char *prefix);

/* The default prefix, `<namespace>_<name>_`, or an empty one if the interface's header didn't
 * parse. */
const char *default_prefix(const struct interface *interface, struct arena *arena);

/* The C name of a parameter or field called `name`, which is `name` with a `_` after it when it
 * would meet a keyword of C or C++, a lowercase macro that common headers define, or one of the
 * generator's own parameters. */
const char *c_member_name(const char *name, struct arena *arena);

/* The C names that come after the prefix: a record's, a kind of host object's, a host function's
 * in `group`, a method's, and an entry point's typed caller's in `script`. */
const char *c_record_name(const struct record *record, struct arena *arena);
const char *c_object_name(const struct object *object, struct arena *arena);
const char *c_function_name(const struct group *group, const struct function *function,
                            struct arena *arena);
const char *c_method_name(const struct function *method, struct arena *arena);
const char *c_entry_point_name(const struct script *script, const struct function *entry_point,
                               struct arena *arena);

/* `name`, in uppercase. */
const char *c_uppercase(const char *name, struct arena *arena);

/* Reports every two declarations whose C names, after `prefix`, are the same. */
void check_c_names(const struct interface *interface, const char *prefix, struct arena *arena,
                   struct diagnostics *diagnostics);

#endif
