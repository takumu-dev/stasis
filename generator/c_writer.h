/* What stasis-gen's two writers of C share: the host's side, which `stasis-gen host` writes, and
 * the C bindings, which `stasis-gen c` writes. Both write the interface's records as structs and
 * its doc comments as Doxygen comments, and break long lines alike.
 *
 * They differ in two of the types that cross: on the host's side, a string is a `stasis_string` and
 * a handle a pointer to its kind's opaque type, and in the C bindings, a string is
 * `<prefix>string` and a handle a struct of its own. */

#ifndef STASIS_GENERATOR_C_WRITER_H
#define STASIS_GENERATOR_C_WRITER_H

#include "arena.h"
#include "interface.h"
#include "text.h"

#include <stdbool.h>
#include <stddef.h>

/* What the strings and handles in each record are: whether it holds a string, and whether it holds
 * a handle, directly or in a record of its own. Each is indexed by the record's index. */
struct record_contents {
    bool *strings;
    bool *handles;
};

/* The state of writing C. */
struct writer {
    struct arena *arena;
    const struct interface *interface;
    /* The prefix of every C name, and of every macro's. */
    const char *prefix;
    const char *macro_prefix;
    /* Whether it writes the C bindings, rather than the host's side. */
    bool bindings;
    /* The file being written. */
    struct text *out;
    /* The columns that write_list leaves free, as a macro's lines need for the indent before them
     * and the `\` after them. */
    size_t margin;
    /* Every record, ordered so that each comes after the records it holds, and otherwise in the
     * order the file declares them. */
    const struct record **records;
    /* Every record, by its index. */
    const struct record **records_by_index;
    struct record_contents contents;
    /* Every group, ordered so that each comes after the groups it uses, and otherwise in the order
     * the file declares them. */
    const struct group **groups;
};

/* Starts writing C for `interface`, which must have been read with no problems and have no C names
 * that clash: the C bindings if `bindings` is set, and the host's side otherwise. Every C name
 * starts with `prefix`. */
void writer_init(struct writer *writer, const struct interface *interface, const char *prefix,
                 bool bindings, struct arena *arena);

/* Adds a string formatted as snprintf does to a list. */
void add_string(struct writer *writer, struct strings *strings, const char *format, ...)
    PRINTF_FORMAT(3, 4);

/* Writes a list, such as a call's arguments: `head`, the items separated by commas, and `tail`.
 * They go on one line at `indent` if that fits, within the writer's margin. Otherwise the items go
 * on the next line, 4 columns further in, or failing that, on a line each. */
void write_list(struct writer *writer, const char *indent, const char *head,
                const struct strings *items, const char *tail);

/* Writes a doc comment at `indent`, of up to three parts, each of which may be left out: its first
 * lines, such as a Doxygen `@brief`, the interface's doc comment, and a paragraph of the
 * generator's own. A comment of one line goes on one line, if it fits, and a first line that
 * doesn't fit goes on in the lines after it. */
void write_doc(struct writer *writer, const char *indent, const char *first, const struct doc *doc,
               const char *paragraph);

/* How `type` is spelt in C. A handle's type on the host's side is a pointer, and its spelling ends
 * in `*`. */
const char *c_type(struct writer *writer, const struct type *type);

/* A pointer to the C type `type`. */
const char *pointer_to(struct writer *writer, const char *type);

/* A declaration of `name` as the C type `type`. */
const char *declaration(struct writer *writer, const char *type, const char *name);

/* A handle to the objects of `object`, as a method's first parameter takes it. */
struct type handle_to(const struct object *object);

/* What a host function or a method is called in a sentence. */
const char *function_word(const struct function *function);

/* A host function, a method or an entry point, as the C it's written into names it. */
struct function_names {
    const struct function *function;
    /* Its qualified name in the interface, such as `world.entity.name`. */
    const char *qualified;
    /* Its C name after the prefix, such as `world_entity_name`, which the names the generated code
     * gives what it writes for it start with too. */
    const char *base;
};

/* The names of a group's host function, of a method, and of an entry point. */
struct function_names function_names(struct writer *writer, const struct group *group,
                                     const struct function *function);
struct function_names method_names(struct writer *writer, const struct function *method);
struct function_names entry_point_names(struct writer *writer, const struct script *script,
                                        const struct function *entry_point);

/* Whether a value of `type` holds a string. */
bool holds_string(const struct writer *writer, const struct type *type);

/* Whether a value of `type` holds a handle. */
bool holds_handle(const struct writer *writer, const struct type *type);

/* The interface's namespace, name and version, as in `acme:dungeon@1.2.0`. */
const char *interface_title(const struct writer *writer);

/* Writes the line that starts every generated file, which names the interface file it comes from
 * as `input`. */
void write_banner(struct writer *writer, const char *input);

/* Writes a record's struct. */
void write_record_declaration(struct writer *writer, const struct record *record);

/* Adds the C declaration of each of a function's parameters, after a method's handle to its object,
 * `self`. */
void add_parameter_declarations(struct writer *writer, struct strings *declarations,
                                const struct function *function);

/* Writes a member group's opening: the Doxygen comment that names it, and its `@{`. */
void write_section_start(struct writer *writer, const char *name, const struct doc *doc);

/* Writes a member group's closing. */
void write_section_end(struct writer *writer);

#endif
