/* An interface as stasis-gen reads it from an interface file, and the function that reads one.
 *
 * Reading an interface parses its file, checks it against the format's rules and resolves every
 * type it names. It reports each problem it finds and carries on past it, so that one reading
 * reports every problem in a file. An interface read with no problems is whole: every name in it
 * follows the rules, and every type is resolved.
 *
 * Every list holds its items in the order the file declares them. */

#ifndef STASIS_GENERATOR_INTERFACE_H
#define STASIS_GENERATOR_INTERFACE_H

#include "arena.h"
#include "diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A doc comment: the text after each line's ///, in order. */
struct doc {
    const char *const *lines;
    size_t count;
};

/* A name, as the file spells it, and where it is. */
struct name {
    const char *text;
    struct position position;
};

/* What a type is: a built-in type, from u8 to string, a record or an object, or a name that isn't
 * resolved yet, or can't be. */
enum type_kind {
    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    TYPE_U64,
    TYPE_S8,
    TYPE_S16,
    TYPE_S32,
    TYPE_S64,
    TYPE_F32,
    TYPE_F64,
    TYPE_BOOL,
    TYPE_STRING,
    /* A record, by value. */
    TYPE_RECORD,
    /* A handle to a kind of host object. */
    TYPE_OBJECT,
    /* A name that isn't resolved yet. */
    TYPE_NAMED,
    /* A type that reading has reported a problem with. */
    TYPE_INVALID,
};

/* A type, as a parameter, a result or a field names it. */
struct type {
    enum type_kind kind;
    /* Its name, as the file spells it. */
    struct name name;
    /* What it resolves to, for TYPE_RECORD and TYPE_OBJECT. */
    const struct record *record;
    const struct object *object;
};

/* One of a record's fields. */
struct field {
    struct field *next;
    struct name name;
    struct doc doc;
    struct type type;
};

/* A record, which holds its fields by value. */
struct record {
    struct name name;
    struct doc doc;
    /* The group that declares it. */
    const struct group *group;
    struct field *fields;
    /* Its place among every record of the interface, from 0. */
    size_t index;
};

/* One of a function's parameters. */
struct parameter {
    struct parameter *next;
    struct name name;
    struct type type;
};

/* A host function, a method or an entry point. */
struct function {
    /* The next method of its object, or the next entry point of its kind of script. */
    struct function *next;
    struct name name;
    struct doc doc;
    struct parameter *parameters;
    bool has_result;
    struct type result;
    /* The object a method belongs to, whose handle is its first parameter. Null for others. */
    const struct object *object;
    /* Whether an entry point is optional. */
    bool optional;
};

/* A kind of host object. */
struct object {
    struct name name;
    struct doc doc;
    /* The group that declares it. */
    const struct group *group;
    struct function *methods;
    /* Its place among every object of the interface, from 0. */
    size_t index;
};

/* One name that a `use` brings in. */
struct use_item {
    struct use_item *next;
    struct name name;
    /* What it brings in, once resolved: a record or an object. */
    const struct record *record;
    const struct object *object;
};

/* A `use` of another group's records and objects. */
struct use {
    /* The group it uses, as the file names it, and once resolved. */
    struct name group_name;
    const struct group *group;
    struct use_item *items;
};

/* What a declaration in a group is. */
enum item_kind {
    ITEM_RECORD,
    ITEM_OBJECT,
    ITEM_FUNCTION,
    ITEM_USE,
};

/* One declaration in a group. */
struct item {
    struct item *next;
    enum item_kind kind;

    union {
        struct record *record;
        struct object *object;
        struct function *function;
        struct use *use;
    } as;
};

/* A group: a named set of host functions, records and kinds of host object. */
struct group {
    struct group *next;
    struct name name;
    struct doc doc;
    struct item *items;
    /* Its place among the interface's groups, from 0. */
    size_t index;
};

/* A group that a kind of script's `uses` names. */
struct script_use {
    struct script_use *next;
    struct name name;
    /* The group, once resolved. */
    const struct group *group;
};

/* A kind of script. */
struct script {
    struct script *next;
    struct name name;
    struct doc doc;
    struct script_use *uses;
    struct function *entry_points;
};

/* The interface's version, `<major>.<minor>.<patch>`. */
struct version {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
};

/* An interface: its header, its groups and its kinds of script. */
struct interface {
    struct doc doc;
    /* Where its header is. */
    struct position position;
    /* Its namespace and name, as in `interface <space>:<name>@<version>;`. */
    struct name space;
    struct name name;
    struct version version;
    struct group *groups;
    size_t group_count;
    size_t record_count;
    size_t object_count;
    /* Its kinds of script, of which a whole interface has exactly one. */
    struct script *scripts;
};

/* Reads the interface in `source`, which holds `size` bytes, allocating it in `arena` and reporting
 * every problem with it to `diagnostics`. */
struct interface *read_interface(const char *source, size_t size, struct arena *arena,
                                 struct diagnostics *diagnostics);

#endif
