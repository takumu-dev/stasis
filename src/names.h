/* The names that tie a script to its interface: its imports' module names and its entry points'
 * export names, each of which carries the interface, its line, and for an entry point, its kind of
 * script and the hash of its signature:
 *
 *     <namespace>:<name>/<group>@<line>                          an import's module
 *     <namespace>:<name>/<kind>@<line>#<entry point>:<hash>      an entry point's export
 *
 * as in `acme:dungeon/world@1` and `acme:dungeon/npc@1#tick:37ab64a28242fa4b`. Each name in them
 * is snake_case, the line is a major version or `0.<minor>`, and the hash is 16 lowercase
 * hexadecimal digits. */

#ifndef STASIS_NAMES_H
#define STASIS_NAMES_H

#include <stasis.h>

#include "signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A line: a major version of at least 1, or 0 and a minor version. */
struct line {
    uint32_t major;
    uint32_t minor;
};

/* A name that names the interface and its line, with what follows: a group for an import's module,
 * or a kind of script, an entry point and a hash for an export. */
struct wire_name {
    stasis_string space;
    stasis_string name;
    /* The group or the kind of script. */
    stasis_string scope;
    struct line line;
    /* For an export, the entry point and its hash. */
    stasis_string entry_point;
    stasis_string hash;
};

/* Whether an export's name is one that Stasis takes for an entry point's: one with a `:` or a `#`,
 * which no other name Stasis gives meaning to has. */
bool stasis_is_entry_point_name(stasis_string text);

/* Whether an import's module name is one that Stasis takes for a group's: one with a `:`. */
bool stasis_is_group_name(stasis_string text);

/* Parses an entry point's export name. Returns false if it doesn't parse. */
bool stasis_parse_entry_point_name(stasis_string text, struct wire_name *name);

/* Parses an import's module name as a group's. Returns false if it doesn't parse. */
bool stasis_parse_group_name(stasis_string text, struct wire_name *name);

/* Whether `text` holds exactly the C string `string`. */
bool stasis_string_is(stasis_string text, const char *string);

#endif
