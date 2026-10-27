#include "host.h"

#include "arena.h"
#include "c_names.h"
#include "files.h"
#include "interface.h"
#include "text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The descriptor format that the generated data has, which is stasis_interface.h's
 * STASIS_INTERFACE_FORMAT. It's written as a number, so that the data names the format it was
 * generated in even when it's compiled against a later stasis_interface.h. */
enum { DESCRIPTOR_FORMAT = 1 };

/* What the strings and handles in each record are: whether it holds a string, and whether it holds
 * a handle, directly or in a record of its own. Each is indexed by the record's index. */
struct record_contents {
    bool *strings;
    bool *handles;
};

/* The state of writing the host's side. */
struct writer {
    struct arena *arena;
    const struct interface *interface;
    /* The prefix of every C name, and of every macro's. */
    const char *prefix;
    const char *macro_prefix;
    /* The file being written. */
    struct text *out;
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

/* Adds a string formatted as snprintf does to a list. */
static void add_string(struct writer *writer, struct strings *strings, const char *format, ...)
    PRINTF_FORMAT(3, 4);

static void add_string(struct writer *writer, struct strings *strings, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    strings_add(strings, writer->arena, arena_vformat(writer->arena, format, arguments));
    va_end(arguments);
}

/* Writes a list, such as a call's arguments: `head`, the items separated by commas, and `tail`.
 * They go on one line at `indent` if that fits. Otherwise the items go on the next line, 4 columns
 * further in, or failing that, on a line each. */
static void write_list(struct writer *writer, const char *indent, const char *head,
                       const struct strings *items, const char *tail) {
    const size_t joined = joined_length(items);
    if (strlen(indent) + strlen(head) + joined + strlen(tail) <= LINE_LIMIT) {
        text_format(writer->out, "%s%s", indent, head);
        text_append_joined(writer->out, items);
        text_format(writer->out, "%s\n", tail);
        return;
    }
    text_format(writer->out, "%s%s\n", indent, head);
    if (strlen(indent) + 4 + joined + strlen(tail) <= LINE_LIMIT) {
        text_format(writer->out, "%s    ", indent);
        text_append_joined(writer->out, items);
        text_format(writer->out, "%s\n", tail);
        return;
    }
    for (size_t i = 0; i < items->count; i++) {
        text_format(writer->out, "%s    %s%s\n", indent, items->items[i],
                    i + 1 < items->count ? "," : tail);
    }
}

/* Writes an initialiser: `head`, the items in braces, and `tail`. They go on one line at `indent`
 * if that fits, and otherwise each item goes on a line of its own. */
static void write_initialiser(struct writer *writer, const char *indent, const char *head,
                              const struct strings *items, const char *tail) {
    if (strlen(indent) + strlen(head) + joined_length(items) + strlen(tail) + 2 <= LINE_LIMIT) {
        text_format(writer->out, "%s%s{", indent, head);
        text_append_joined(writer->out, items);
        text_format(writer->out, "}%s\n", tail);
        return;
    }
    text_format(writer->out, "%s%s{\n", indent, head);
    for (size_t i = 0; i < items->count; i++) {
        text_format(writer->out, "%s    %s,\n", indent, items->items[i]);
    }
    text_format(writer->out, "%s}%s\n", indent, tail);
}

/* A line of a doc comment from the interface, as it goes after a comment's `*`: with no space at
 * its end, and a space at its start unless it's empty. A `*` and a `/` that would end the comment,
 * or that compilers warn would start another within it, are kept apart by a space. */
static const char *doc_line(struct writer *writer, const char *line) {
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == ' ' || line[length - 1] == '\t')) {
        length--;
    }
    struct text rendered;
    text_init(&rendered, writer->arena);
    if (length > 0 && line[0] != ' ') {
        text_append(&rendered, " ");
    }
    for (size_t i = 0; i < length; i++) {
        text_append_length(&rendered, &line[i], 1);
        if (i + 1 == length) {
            break;
        }
        if ((line[i] == '*' && line[i + 1] == '/') || (line[i] == '/' && line[i + 1] == '*')) {
            text_append(&rendered, " ");
        }
    }
    return rendered.data;
}

/* Writes a doc comment at `indent`, of up to three parts, each of which may be left out: its first
 * lines, such as a Doxygen `@brief`, the interface's doc comment, and a paragraph of the
 * generator's own. A comment of one line goes on one line, if it fits. */
static void write_doc(struct writer *writer, const char *indent, const char *first,
                      const struct doc *doc, const char *paragraph) {
    const size_t doc_count = doc == NULL ? 0 : doc->count;
    bool has_paragraph = false;
    if (paragraph != NULL) {
        has_paragraph = paragraph[0] != '\0';
    }
    if (first == NULL && doc_count == 0 && !has_paragraph) {
        return;
    }
    const char *only = NULL;
    if (first != NULL && strchr(first, '\n') == NULL && doc_count == 0 && !has_paragraph) {
        only = arena_format(writer->arena, " %s", first);
    } else if (first == NULL && doc_count == 1 && !has_paragraph) {
        only = doc_line(writer, doc->lines[0]);
    }
    if (only != NULL && strlen(indent) + strlen(only) + 6 <= LINE_LIMIT) {
        text_format(writer->out, "%s/**%s */\n", indent, only);
        return;
    }

    text_format(writer->out, "%s/**\n", indent);
    bool separate = false;
    while (first != NULL) {
        const char *end = strchr(first, '\n');
        const int length = end == NULL ? (int)strlen(first) : (int)(end - first);
        text_format(writer->out, "%s * %.*s\n", indent, length, first);
        first = end == NULL ? NULL : end + 1;
        separate = true;
    }
    if (doc_count > 0) {
        if (separate) {
            text_format(writer->out, "%s *\n", indent);
        }
        for (size_t i = 0; i < doc_count; i++) {
            text_format(writer->out, "%s *%s\n", indent, doc_line(writer, doc->lines[i]));
        }
        separate = true;
    }
    if (has_paragraph) {
        if (separate) {
            text_format(writer->out, "%s *\n", indent);
        }
        text_fill(writer->out, arena_format(writer->arena, "%s *", indent), paragraph);
    }
    text_format(writer->out, "%s */\n", indent);
}

/* The C type of each built-in type, and the descriptor's name for it. */
static const struct {
    const char *c;
    const char *descriptor;
} built_in_types[] = {
    [TYPE_U8] = {"uint8_t", "STASIS_TYPE_U8"},
    [TYPE_U16] = {"uint16_t", "STASIS_TYPE_U16"},
    [TYPE_U32] = {"uint32_t", "STASIS_TYPE_U32"},
    [TYPE_U64] = {"uint64_t", "STASIS_TYPE_U64"},
    [TYPE_S8] = {"int8_t", "STASIS_TYPE_S8"},
    [TYPE_S16] = {"int16_t", "STASIS_TYPE_S16"},
    [TYPE_S32] = {"int32_t", "STASIS_TYPE_S32"},
    [TYPE_S64] = {"int64_t", "STASIS_TYPE_S64"},
    [TYPE_F32] = {"float", "STASIS_TYPE_F32"},
    [TYPE_F64] = {"double", "STASIS_TYPE_F64"},
    [TYPE_BOOL] = {"bool", "STASIS_TYPE_BOOL"},
    [TYPE_STRING] = {"stasis_string", "STASIS_TYPE_STRING"},
};

/* How `type` is spelt in C. A handle's type is a pointer, and its spelling ends in `*`. */
static const char *c_type(struct writer *writer, const struct type *type) {
    switch (type->kind) {
    case TYPE_RECORD:
        return arena_format(writer->arena, "%s%s", writer->prefix,
                            c_record_name(type->record, writer->arena));
    case TYPE_OBJECT:
        return arena_format(writer->arena, "%s%s *", writer->prefix,
                            c_object_name(type->object, writer->arena));
    default:
        return built_in_types[type->kind].c;
    }
}

/* A pointer to the C type `type`. */
static const char *pointer_to(struct writer *writer, const char *type) {
    return arena_format(writer->arena, "%s%s*", type, type[strlen(type) - 1] == '*' ? "" : " ");
}

/* A declaration of `name` as the C type `type`. */
static const char *declaration(struct writer *writer, const char *type, const char *name) {
    return arena_format(writer->arena, "%s%s%s", type, type[strlen(type) - 1] == '*' ? "" : " ",
                        name);
}

/* A handle to the objects of `object`, as a method's first parameter takes it. */
static struct type handle_to(const struct object *object) {
    struct type type;
    memset(&type, 0, sizeof type);
    type.kind = TYPE_OBJECT;
    type.object = object;
    return type;
}

/* The number of a function's parameters, a method's handle to its object counted as its first. */
static size_t count_parameters(const struct function *function) {
    size_t count = function->object != NULL ? 1 : 0;
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        count++;
    }
    return count;
}

/* The number of a record's fields. */
static size_t count_fields(const struct record *record) {
    size_t count = 0;
    for (const struct field *field = record->fields; field != NULL; field = field->next) {
        count++;
    }
    return count;
}

/* The number of an object's methods. */
static size_t count_methods(const struct object *object) {
    size_t count = 0;
    for (const struct function *method = object->methods; method != NULL; method = method->next) {
        count++;
    }
    return count;
}

/* The number of a group's host functions, leaving out its objects' methods. */
static size_t count_functions(const struct group *group) {
    size_t count = 0;
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind == ITEM_FUNCTION) {
            count++;
        }
    }
    return count;
}

/* The number of a kind of script's entry points. */
static size_t count_entry_points(const struct script *script) {
    size_t count = 0;
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        count++;
    }
    return count;
}

/* What a host function or a method is called in a sentence. */
static const char *function_word(const struct function *function) {
    return function->object != NULL ? "method" : "host function";
}

/* A host function, a method or an entry point, as the host's side names it. */
struct function_names {
    const struct function *function;
    /* Its qualified name in the interface, such as `world.entity.name`. */
    const char *qualified;
    /* Its C name after the prefix, such as `world_entity_name`, which its statics in the source
     * start with too. */
    const char *base;
};

/* Whether a value of `type` holds a string. */
static bool holds_string(const struct writer *writer, const struct type *type) {
    if (type->kind == TYPE_RECORD) {
        return writer->contents.strings[type->record->index];
    }
    return type->kind == TYPE_STRING;
}

/* Whether a value of `type` holds a handle. */
static bool holds_handle(const struct writer *writer, const struct type *type) {
    if (type->kind == TYPE_RECORD) {
        return writer->contents.handles[type->record->index];
    }
    return type->kind == TYPE_OBJECT;
}

/* Whether a record's fields hold only records in `placed`, by index. */
static bool fields_placed(const struct record *record, const bool *placed) {
    for (const struct field *field = record->fields; field != NULL; field = field->next) {
        if (field->type.kind == TYPE_RECORD && !placed[field->type.record->index]) {
            return false;
        }
    }
    return true;
}

/* Orders every record after the records it holds, picking each time the first in the file whose
 * records are all placed, and works out what each holds. */
static void order_records(struct writer *writer) {
    const struct interface *interface = writer->interface;
    const size_t count = interface->record_count;
    const struct record **all =
        (const struct record **)arena_allocate(writer->arena, (count + 1) * sizeof *all);
    writer->records_by_index = all;
    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        for (const struct item *item = group->items; item != NULL; item = item->next) {
            if (item->kind == ITEM_RECORD) {
                all[item->as.record->index] = item->as.record;
            }
        }
    }
    bool *placed = arena_allocate(writer->arena, (count + 1) * sizeof *placed);
    writer->records =
        (const struct record **)arena_allocate(writer->arena, (count + 1) * sizeof *all);
    writer->contents.strings = arena_allocate(writer->arena, (count + 1) * sizeof *placed);
    writer->contents.handles = arena_allocate(writer->arena, (count + 1) * sizeof *placed);
    for (size_t placed_count = 0; placed_count < count; placed_count++) {
        size_t next = 0;
        while (next < count && (placed[next] || !fields_placed(all[next], placed))) {
            next++;
        }
        if (next == count) {
            /* Only a record that holds itself would leave none to place, which reading the
             * interface rules out. */
            break;
        }
        const struct record *record = all[next];
        placed[next] = true;
        writer->records[placed_count] = record;
        for (const struct field *field = record->fields; field != NULL; field = field->next) {
            if (holds_string(writer, &field->type)) {
                writer->contents.strings[next] = true;
            }
            if (holds_handle(writer, &field->type)) {
                writer->contents.handles[next] = true;
            }
        }
    }
}

/* Whether every group that `group` uses is in `placed`, by index. */
static bool uses_placed(const struct group *group, const bool *placed) {
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind == ITEM_USE && !placed[item->as.use->group->index]) {
            return false;
        }
    }
    return true;
}

/* Orders every group after the groups it uses, picking each time the first in the file whose used
 * groups are all placed. */
static void order_groups(struct writer *writer) {
    const size_t count = writer->interface->group_count;
    const struct group **all =
        (const struct group **)arena_allocate(writer->arena, (count + 1) * sizeof *all);
    for (const struct group *group = writer->interface->groups; group != NULL;
         group = group->next) {
        all[group->index] = group;
    }
    bool *placed = arena_allocate(writer->arena, (count + 1) * sizeof *placed);
    writer->groups =
        (const struct group **)arena_allocate(writer->arena, (count + 1) * sizeof *all);
    for (size_t placed_count = 0; placed_count < count; placed_count++) {
        size_t next = 0;
        while (next < count && (placed[next] || !uses_placed(all[next], placed))) {
            next++;
        }
        if (next == count) {
            /* Only groups that use each other in a cycle would leave none to place, which reading
             * the interface rules out. */
            break;
        }
        placed[next] = true;
        writer->groups[placed_count] = all[next];
    }
}

/* The names of a group's host function. */
static struct function_names function_names(struct writer *writer, const struct group *group,
                                            const struct function *function) {
    struct function_names names;
    names.function = function;
    names.qualified = arena_format(writer->arena, "%s.%s", group->name.text, function->name.text);
    names.base = c_function_name(group, function, writer->arena);
    return names;
}

/* The names of a method. */
static struct function_names method_names(struct writer *writer, const struct function *method) {
    struct function_names names;
    names.function = method;
    names.qualified = arena_format(writer->arena, "%s.%s.%s", method->object->group->name.text,
                                   method->object->name.text, method->name.text);
    names.base = c_method_name(method, writer->arena);
    return names;
}

/* The names of an entry point. */
static struct function_names entry_point_names(struct writer *writer, const struct script *script,
                                               const struct function *entry_point) {
    struct function_names names;
    names.function = entry_point;
    names.qualified =
        arena_format(writer->arena, "%s.%s", script->name.text, entry_point->name.text);
    names.base = c_entry_point_name(script, entry_point, writer->arena);
    return names;
}

/* The interface's namespace, name and version, as in `acme:dungeon@1.2.0`. */
static const char *interface_title(const struct writer *writer) {
    const struct interface *interface = writer->interface;
    return arena_format(writer->arena, "%s:%s@%lu.%lu.%lu", interface->space.text,
                        interface->name.text, (unsigned long)interface->version.major,
                        (unsigned long)interface->version.minor,
                        (unsigned long)interface->version.patch);
}

/* The stack that a host function has free when a script calls it, as text such as `16 KiB`. */
static const char *host_function_stack(struct writer *writer) {
    const unsigned long size = STASIS_HOST_FUNCTION_STACK;
    if (size % 1024 == 0) {
        return arena_format(writer->arena, "%lu KiB", size / 1024);
    }
    return arena_format(writer->arena, "%lu bytes", size);
}

/* Writes the line that starts every generated file. */
static void write_banner(struct writer *writer, const char *input) {
    text_format(writer->out, "/* Generated by stasis-gen %s from %s. Do not edit. */\n",
                STASIS_VERSION, input);
}

/* Writes the header's declarations of a kind of host object: its opaque type and its id. */
static void write_object_declarations(struct writer *writer, const struct object *object) {
    const char *name = c_object_name(object, writer->arena);
    const char *qualified =
        arena_format(writer->arena, "%s.%s", object->group->name.text, object->name.text);
    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena, "@brief The kind of host object `%s`.", qualified),
              &object->doc,
              "The type is opaque: a host object of this kind crosses as a pointer to it, which "
              "the host converts to and from a pointer to its own object.");
    text_format(writer->out, "typedef struct %s%s %s%s;\n", writer->prefix, name, writer->prefix,
                name);
    text_append(writer->out, "\n");
    write_doc(
        writer, "",
        arena_format(writer->arena, "@brief The id of the kind of host object `%s`.", qualified),
        NULL, NULL);
    text_format(writer->out, "#define %s%s_KIND %lu\n", writer->macro_prefix,
                c_uppercase(name, writer->arena), (unsigned long)object->index);
}

/* Writes a record's struct. */
static void write_record_declaration(struct writer *writer, const struct record *record) {
    const char *name =
        arena_format(writer->arena, "%s%s", writer->prefix, c_record_name(record, writer->arena));
    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena, "@brief The record `%s.%s`.", record->group->name.text,
                           record->name.text),
              &record->doc, NULL);
    text_format(writer->out, "typedef struct %s {\n", name);
    for (const struct field *field = record->fields; field != NULL; field = field->next) {
        write_doc(writer, "    ", NULL, &field->doc, NULL);
        text_format(writer->out, "    %s;\n",
                    declaration(writer, c_type(writer, &field->type),
                                c_member_name(field->name.text, writer->arena)));
    }
    text_format(writer->out, "} %s;\n", name);
}

/* What a host function's documentation says beyond the interface's doc comment. */
static const char *host_function_rules(struct writer *writer, const struct function *function) {
    struct text rules;
    text_init(&rules, writer->arena);
    text_append(&rules, "The host defines it, for scripts to call.");
    bool string_arguments = false;
    bool handle_arguments = false;
    if (function->object != NULL) {
        text_append(&rules, " `self` is the host object it's called on.");
        handle_arguments = true;
    }
    text_format(&rules,
                " `instance` is the script instance that calls it, on the thread and stack that "
                "called into that instance, with at least %s of the stack free. It returns on the "
                "same thread and stack: it never switches fibers or calls `longjmp` in the middle "
                "of the call.",
                host_function_stack(writer));
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        if (holds_string(writer, &parameter->type)) {
            string_arguments = true;
        }
        if (holds_handle(writer, &parameter->type)) {
            handle_arguments = true;
        }
    }
    if (string_arguments) {
        text_append(&rules, " The strings in its arguments point into the instance's memory, and "
                            "are valid only until it returns.");
    }
    if (handle_arguments) {
        text_append(&rules, " A host object among its arguments is null when the instance's "
                            "handle to it has been revoked.");
    }
    if (function->has_result && holds_string(writer, &function->result)) {
        text_append(&rules, " The strings it returns must point at storage that outlives the "
                            "call, such as a host object's own name.");
    }
    if (function->has_result && holds_handle(writer, &function->result)) {
        text_append(&rules, " A host object it returns can't be null: a null one aborts the "
                            "call.");
    }
    return rules.data;
}

/* Adds the C declaration of each of a function's parameters, after a method's handle to its object,
 * `self`. */
static void add_parameter_declarations(struct writer *writer, struct strings *declarations,
                                       const struct function *function) {
    if (function->object != NULL) {
        const struct type self = handle_to(function->object);
        add_string(writer, declarations, "%s", declaration(writer, c_type(writer, &self), "self"));
    }
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        add_string(writer, declarations, "%s",
                   declaration(writer, c_type(writer, &parameter->type),
                               c_member_name(parameter->name.text, writer->arena)));
    }
}

/* Writes the prototype of a host function or a method, which the host defines. */
static void write_function_declaration(struct writer *writer, const struct function_names *names) {
    const struct function *function = names->function;
    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena, "@brief The %s `%s`.", function_word(function),
                           names->qualified),
              &function->doc, host_function_rules(writer, function));
    struct strings parameters = {0};
    add_string(writer, &parameters, "stasis_instance *instance");
    add_parameter_declarations(writer, &parameters, function);
    const char *result = function->has_result ? c_type(writer, &function->result) : "void";
    const char *head =
        arena_format(writer->arena, "%s(",
                     declaration(writer, result,
                                 arena_format(writer->arena, "%s%s", writer->prefix, names->base)));
    write_list(writer, "", head, &parameters, ");");
}

/* Writes a member group's opening: the Doxygen comment that names it, and its `@{`. */
static void write_section_start(struct writer *writer, const char *name, const struct doc *doc) {
    text_append(writer->out, "\n");
    write_doc(writer, "", name, doc, NULL);
    text_append(writer->out, "/** @{ */\n");
}

/* Writes a member group's closing. */
static void write_section_end(struct writer *writer) { text_append(writer->out, "\n/** @} */\n"); }

/* Whether a group declares anything in C: a record, an object or a host function. */
static bool declares_anything(const struct group *group) {
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind != ITEM_USE) {
            return true;
        }
    }
    return false;
}

/* Writes the header's declarations for a group, in a member group of their own: its kinds of host
 * object, its records, and its host functions and methods. */
static void write_group_declarations(struct writer *writer, const struct group *group) {
    if (!declares_anything(group)) {
        return;
    }
    write_section_start(writer, arena_format(writer->arena, "@name The group %s", group->name.text),
                        &group->doc);
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind == ITEM_OBJECT) {
            write_object_declarations(writer, item->as.object);
        }
    }
    for (size_t i = 0; i < writer->interface->record_count; i++) {
        if (writer->records[i]->group == group) {
            write_record_declaration(writer, writer->records[i]);
        }
    }
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind == ITEM_FUNCTION) {
            const struct function_names names = function_names(writer, group, item->as.function);
            write_function_declaration(writer, &names);
        } else if (item->kind == ITEM_OBJECT) {
            for (const struct function *method = item->as.object->methods; method != NULL;
                 method = method->next) {
                const struct function_names names = method_names(writer, method);
                write_function_declaration(writer, &names);
            }
        }
    }
    write_section_end(writer);
}

/* What a typed caller's documentation says beyond the interface's doc comment. */
static const char *typed_caller_rules(struct writer *writer, const struct function *entry_point) {
    struct text rules;
    text_init(&rules, writer->arena);
    text_append(&rules, "It calls the entry point of `instance` through stasis_call(), and returns "
                        "how the call ended.");
    if (entry_point->optional) {
        text_append(&rules, " The entry point is optional, and a script that doesn't provide it "
                            "refuses the call with ::STASIS_REFUSED_ABSENT.");
    }
    if (entry_point->has_result) {
        text_append(&rules, " It writes the entry point's result through `result`, which is "
                            "zeroed when the call fails.");
        if (holds_string(writer, &entry_point->result)) {
            text_append(&rules, " The strings in the result stay valid until the next call into "
                                "the instance, or until it's destroyed.");
        }
    }
    return rules.data;
}

/* The parameters of an entry point's typed caller. */
static struct strings typed_caller_parameters(struct writer *writer,
                                              const struct function *entry_point) {
    struct strings parameters = {0};
    add_string(writer, &parameters, "stasis_instance *instance");
    add_parameter_declarations(writer, &parameters, entry_point);
    if (entry_point->has_result) {
        add_string(writer, &parameters, "%s",
                   declaration(writer, pointer_to(writer, c_type(writer, &entry_point->result)),
                               "result"));
    }
    return parameters;
}

/* Writes the header's declarations for a kind of script, in a member group of their own: each
 * entry point's id and typed caller. */
static void write_script_declarations(struct writer *writer, const struct script *script) {
    if (script->entry_points == NULL) {
        return;
    }
    write_section_start(
        writer, arena_format(writer->arena, "@name The kind of script %s", script->name.text),
        &script->doc);
    size_t id = 0;
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        const struct function_names names = entry_point_names(writer, script, entry_point);
        text_append(writer->out, "\n");
        write_doc(
            writer, "",
            arena_format(writer->arena, "@brief The id of the entry point `%s`.", names.qualified),
            NULL, NULL);
        text_format(writer->out, "#define %s%s_ENTRY %lu\n", writer->macro_prefix,
                    c_uppercase(names.base, writer->arena), (unsigned long)id++);
        text_append(writer->out, "\n");
        write_doc(
            writer, "",
            arena_format(writer->arena, "@brief Calls the entry point `%s`.", names.qualified),
            &entry_point->doc, typed_caller_rules(writer, entry_point));
        const struct strings parameters = typed_caller_parameters(writer, entry_point);
        write_list(writer, "",
                   arena_format(writer->arena, "stasis_status %s%s(", writer->prefix, names.base),
                   &parameters, ");");
    }
    write_section_end(writer);
}

/* Writes the header. */
static void write_header(struct writer *writer, const char *base, const char *input) {
    const char *title = interface_title(writer);
    const char *guard = arena_format(writer->arena, "%sH", writer->macro_prefix);
    write_banner(writer, input);
    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena,
                           "@file %s.h\n@brief The host's side of the interface `%s`.", base,
                           title),
              &writer->interface->doc,
              arena_format(writer->arena,
                           "The host defines each host function declared here, for scripts to "
                           "call, and calls scripts' entry points through the typed callers "
                           "declared here. It loads each script against ::%sinterface.",
                           writer->prefix));
    text_format(writer->out,
                "\n#ifndef %s\n#define %s\n\n#include <stasis.h>\n\n#include <stdbool.h>\n"
                "#include <stdint.h>\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n",
                guard, guard);
    write_doc(writer, "",
              arena_format(writer->arena, "@brief The interface `%s`, as Stasis reads it.", title),
              NULL, "The host loads each script against it.");
    text_format(writer->out, "extern const stasis_interface %sinterface;\n", writer->prefix);
    for (size_t i = 0; i < writer->interface->group_count; i++) {
        write_group_declarations(writer, writer->groups[i]);
    }
    for (const struct script *script = writer->interface->scripts; script != NULL;
         script = script->next) {
        write_script_declarations(writer, script);
    }
    text_append(writer->out, "\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n");
}

/* Adds the designated initialisers of a value's descriptor: its type, its record's index or its
 * kind of host object's id, and, unless it's a result, its offset in its struct. */
static void add_value(struct writer *writer, struct strings *items, const struct type *type,
                      const char *offset) {
    if (type->kind == TYPE_RECORD) {
        add_string(writer, items, ".type = STASIS_TYPE_RECORD");
        add_string(writer, items, ".index = %lu /* %s.%s */", (unsigned long)type->record->index,
                   type->record->group->name.text, type->record->name.text);
    } else if (type->kind == TYPE_OBJECT) {
        add_string(writer, items, ".type = STASIS_TYPE_HANDLE");
        add_string(writer, items, ".index = %s%s_KIND", writer->macro_prefix,
                   c_uppercase(c_object_name(type->object, writer->arena), writer->arena));
    } else {
        add_string(writer, items, ".type = %s", built_in_types[type->kind].descriptor);
    }
    if (offset != NULL) {
        add_string(writer, items, ".offset = %s", offset);
    }
}

/* Writes the descriptors of a record's fields. */
static void write_record_fields(struct writer *writer, const struct record *record) {
    const char *name = c_record_name(record, writer->arena);
    text_format(writer->out,
                "\n/* The record %s.%s. */\n\nstatic const stasis_field %s_fields_[] = {\n",
                record->group->name.text, record->name.text, name);
    for (const struct field *field = record->fields; field != NULL; field = field->next) {
        struct strings value = {0};
        add_value(writer, &value, &field->type,
                  arena_format(writer->arena, "offsetof(%s%s, %s)", writer->prefix, name,
                               c_member_name(field->name.text, writer->arena)));
        const char *head =
            arena_format(writer->arena, "{.name = \"%s\", .value = ", field->name.text);
        if (strlen(head) + joined_length(&value) + 10 <= LINE_LIMIT) {
            write_initialiser(writer, "    ", head, &value, "},");
            continue;
        }
        text_format(writer->out, "    {\n        .name = \"%s\",\n", field->name.text);
        write_initialiser(writer, "        ", ".value = ", &value, ",");
        text_append(writer->out, "    },\n");
    }
    text_append(writer->out, "};\n");
}

/* Writes the struct that holds a function's arguments, `struct <base>_arguments_`, unless it takes
 * none. A method's first argument is the handle of its object, `self`. */
static void write_arguments_struct(struct writer *writer, const struct function_names *names) {
    if (count_parameters(names->function) == 0) {
        return;
    }
    struct strings members = {0};
    add_parameter_declarations(writer, &members, names->function);
    text_format(writer->out, "\nstruct %s_arguments_ {\n", names->base);
    for (size_t i = 0; i < members.count; i++) {
        text_format(writer->out, "    %s;\n", members.items[i]);
    }
    text_append(writer->out, "};\n");
}

/* Writes the descriptors of a function's parameters and result, `<base>_parameters_` and
 * `<base>_result_`, for each it has. */
static void write_parameters_and_result(struct writer *writer, const struct function_names *names) {
    const struct function *function = names->function;
    if (count_parameters(function) > 0) {
        text_format(writer->out, "\nstatic const stasis_value %s_parameters_[] = {\n", names->base);
        if (function->object != NULL) {
            const struct type self = handle_to(function->object);
            struct strings value = {0};
            add_value(
                writer, &value, &self,
                arena_format(writer->arena, "offsetof(struct %s_arguments_, self)", names->base));
            write_initialiser(writer, "    ", "", &value, ",");
        }
        for (const struct parameter *parameter = function->parameters; parameter != NULL;
             parameter = parameter->next) {
            struct strings value = {0};
            add_value(writer, &value, &parameter->type,
                      arena_format(writer->arena, "offsetof(struct %s_arguments_, %s)", names->base,
                                   c_member_name(parameter->name.text, writer->arena)));
            write_initialiser(writer, "    ", "", &value, ",");
        }
        text_append(writer->out, "};\n");
    }
    if (function->has_result) {
        struct strings value = {0};
        add_value(writer, &value, &function->result, NULL);
        text_append(writer->out, "\n");
        write_initialiser(
            writer, "",
            arena_format(writer->arena, "static const stasis_value %s_result_ = ", names->base),
            &value, ";");
    }
}

/* Writes a host function's or a method's thunk, `<base>_thunk_`. */
static void write_thunk(struct writer *writer, const struct function_names *names) {
    const struct function *function = names->function;
    struct strings parameters = {0};
    add_string(writer, &parameters, "stasis_instance *instance");
    add_string(writer, &parameters, "const void *arguments");
    add_string(writer, &parameters, "void *result");
    text_append(writer->out, "\n");
    write_list(writer, "", arena_format(writer->arena, "static void %s_thunk_(", names->base),
               &parameters, ") {");
    if (count_parameters(function) > 0) {
        text_format(writer->out, "    const struct %s_arguments_ *a = arguments;\n", names->base);
    } else {
        text_append(writer->out, "    (void)arguments;\n");
    }
    struct strings arguments = {0};
    add_string(writer, &arguments, "instance");
    if (function->object != NULL) {
        add_string(writer, &arguments, "a->self");
    }
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        add_string(writer, &arguments, "a->%s", c_member_name(parameter->name.text, writer->arena));
    }
    const char *call = arena_format(writer->arena, "%s%s(", writer->prefix, names->base);
    if (function->has_result) {
        call = arena_format(writer->arena, "*(%s)result = %s",
                            pointer_to(writer, c_type(writer, &function->result)), call);
    } else {
        text_append(writer->out, "    (void)result;\n");
    }
    write_list(writer, "    ", call, &arguments, ");");
    text_append(writer->out, "}\n");
}

/* Writes everything the source holds for a host function or a method, under a comment that names
 * it. */
static void write_function_source(struct writer *writer, const struct function_names *names) {
    text_format(writer->out, "\n/* The %s %s. */\n", function_word(names->function),
                names->qualified);
    write_arguments_struct(writer, names);
    write_thunk(writer, names);
    write_parameters_and_result(writer, names);
}

/* Writes the descriptor of a host function or a method, as an element of an array. */
static void write_function_descriptor(struct writer *writer, const struct function_names *names) {
    const struct function *function = names->function;
    struct strings items = {0};
    add_string(writer, &items, ".name = \"%s\"", function->name.text);
    const size_t parameter_count = count_parameters(function);
    if (parameter_count > 0) {
        add_string(writer, &items, ".parameters = %s_parameters_", names->base);
        add_string(writer, &items, ".parameter_count = %lu", (unsigned long)parameter_count);
        add_string(writer, &items, ".arguments_size = sizeof(struct %s_arguments_)", names->base);
    }
    if (function->has_result) {
        add_string(writer, &items, ".result = &%s_result_", names->base);
    }
    add_string(writer, &items, ".thunk = %s_thunk_", names->base);
    write_initialiser(writer, "    ", "", &items, ",");
}

/* Writes everything the source holds for a group: its records' fields, its host functions and
 * methods, and the arrays of their descriptors. */
static void write_group_source(struct writer *writer, const struct group *group) {
    bool has_functions = false;
    bool has_methods = false;
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind == ITEM_RECORD) {
            write_record_fields(writer, item->as.record);
        } else if (item->kind == ITEM_FUNCTION) {
            has_functions = true;
            const struct function_names names = function_names(writer, group, item->as.function);
            write_function_source(writer, &names);
        } else if (item->kind == ITEM_OBJECT) {
            for (const struct function *method = item->as.object->methods; method != NULL;
                 method = method->next) {
                has_methods = true;
                const struct function_names names = method_names(writer, method);
                write_function_source(writer, &names);
            }
        }
    }
    if (!has_functions && !has_methods) {
        return;
    }
    text_format(writer->out, "\n/* The group %s. */\n", group->name.text);
    if (has_functions) {
        text_format(writer->out, "\nstatic const stasis_host_function %s_functions_[] = {\n",
                    group->name.text);
        for (const struct item *item = group->items; item != NULL; item = item->next) {
            if (item->kind == ITEM_FUNCTION) {
                const struct function_names names =
                    function_names(writer, group, item->as.function);
                write_function_descriptor(writer, &names);
            }
        }
        text_append(writer->out, "};\n");
    }
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind != ITEM_OBJECT || item->as.object->methods == NULL) {
            continue;
        }
        text_format(writer->out, "\nstatic const stasis_host_function %s_methods_[] = {\n",
                    c_object_name(item->as.object, writer->arena));
        for (const struct function *method = item->as.object->methods; method != NULL;
             method = method->next) {
            const struct function_names names = method_names(writer, method);
            write_function_descriptor(writer, &names);
        }
        text_append(writer->out, "};\n");
    }
}

/* Writes the array of the interface's records, in the order of their indexes. */
static void write_records(struct writer *writer) {
    text_append(writer->out, "\n/* The interface's records. */\n\n"
                             "static const stasis_record records_[] = {\n");
    for (size_t index = 0; index < writer->interface->record_count; index++) {
        const struct record *record = writer->records_by_index[index];
        const size_t field_count = count_fields(record);
        const char *name = c_record_name(record, writer->arena);
        struct strings items = {0};
        add_string(writer, &items, ".name = \"%s\"", record->name.text);
        add_string(writer, &items, ".group = %lu", (unsigned long)record->group->index);
        add_string(writer, &items, ".fields = %s_fields_", name);
        add_string(writer, &items, ".field_count = %lu", (unsigned long)field_count);
        add_string(writer, &items, ".size = sizeof(%s%s)", writer->prefix, name);
        write_initialiser(writer, "    ", "", &items, ",");
    }
    text_append(writer->out, "};\n");
}

/* Writes the descriptor of a kind of host object, as an element of an array. */
static void write_kind(struct writer *writer, const struct object *object) {
    const size_t method_count = count_methods(object);
    struct strings items = {0};
    add_string(writer, &items, ".name = \"%s\"", object->name.text);
    add_string(writer, &items, ".qualified_name = \"%s.%s\"", object->group->name.text,
               object->name.text);
    add_string(writer, &items, ".group = %lu", (unsigned long)object->group->index);
    if (method_count > 0) {
        add_string(writer, &items, ".methods = %s_methods_", c_object_name(object, writer->arena));
        add_string(writer, &items, ".method_count = %lu", (unsigned long)method_count);
    }
    write_initialiser(writer, "    ", "", &items, ",");
}

/* Writes the array of the interface's kinds of host object, in the order of their ids. */
static void write_kinds(struct writer *writer) {
    text_append(writer->out, "\n/* The interface's kinds of host object. */\n\n"
                             "static const stasis_object_kind kinds_[] = {\n");
    for (const struct group *group = writer->interface->groups; group != NULL;
         group = group->next) {
        for (const struct item *item = group->items; item != NULL; item = item->next) {
            if (item->kind == ITEM_OBJECT) {
                write_kind(writer, item->as.object);
            }
        }
    }
    text_append(writer->out, "};\n");
}

/* Writes the array of the interface's groups. */
static void write_groups(struct writer *writer) {
    text_append(writer->out, "\n/* The interface's groups. */\n\n"
                             "static const stasis_group groups_[] = {\n");
    for (const struct group *group = writer->interface->groups; group != NULL;
         group = group->next) {
        const size_t function_count = count_functions(group);
        struct strings items = {0};
        add_string(writer, &items, ".name = \"%s\"", group->name.text);
        if (function_count > 0) {
            add_string(writer, &items, ".functions = %s_functions_", group->name.text);
            add_string(writer, &items, ".function_count = %lu", (unsigned long)function_count);
        }
        write_initialiser(writer, "    ", "", &items, ",");
    }
    text_append(writer->out, "};\n");
}

/* Writes everything the source holds for a kind of script: its entry points' argument structs and
 * descriptors. */
static void write_script_source(struct writer *writer, const struct script *script) {
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        if (entry_point->parameters == NULL && !entry_point->has_result) {
            continue;
        }
        const struct function_names names = entry_point_names(writer, script, entry_point);
        text_format(writer->out, "\n/* The entry point %s. */\n", names.qualified);
        write_arguments_struct(writer, &names);
        write_parameters_and_result(writer, &names);
    }
    if (script->entry_points == NULL) {
        return;
    }
    text_format(writer->out,
                "\n/* The kind of script %s. */\n\nstatic const stasis_entry_point "
                "%s_entry_points_[] = {\n",
                script->name.text, script->name.text);
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        const struct function_names names = entry_point_names(writer, script, entry_point);
        const size_t parameter_count = count_parameters(entry_point);
        struct strings items = {0};
        add_string(writer, &items, ".name = \"%s\"", entry_point->name.text);
        if (entry_point->optional) {
            add_string(writer, &items, ".optional = true");
        }
        if (parameter_count > 0) {
            add_string(writer, &items, ".parameters = %s_parameters_", names.base);
            add_string(writer, &items, ".parameter_count = %lu", (unsigned long)parameter_count);
        }
        if (entry_point->has_result) {
            add_string(writer, &items, ".result = &%s_result_", names.base);
        }
        write_initialiser(writer, "    ", "", &items, ",");
    }
    text_append(writer->out, "};\n");
}

/* A version number as a C constant, which is unsigned if it's too large for a 32-bit int, so that
 * no compiler sees a wider type narrowed. */
static const char *version_number(struct writer *writer, uint32_t number) {
    return arena_format(writer->arena, "%lu%s", (unsigned long)number,
                        number > INT32_MAX ? "u" : "");
}

/* Writes the interface's kinds of script, and the interface itself. */
static void write_interface_definition(struct writer *writer) {
    const struct interface *interface = writer->interface;
    size_t script_count = 0;
    text_append(writer->out, "\n/* The interface's kinds of script. */\n\n"
                             "static const stasis_script_kind scripts_[] = {\n");
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        const size_t entry_point_count = count_entry_points(script);
        struct strings items = {0};
        add_string(writer, &items, ".name = \"%s\"", script->name.text);
        if (entry_point_count > 0) {
            add_string(writer, &items, ".entry_points = %s_entry_points_", script->name.text);
            add_string(writer, &items, ".entry_point_count = %lu",
                       (unsigned long)entry_point_count);
        }
        write_initialiser(writer, "    ", "", &items, ",");
        script_count++;
    }
    text_append(writer->out, "};\n\n/* The interface. */\n\n");

    struct strings items = {0};
    add_string(writer, &items, ".format = %d", DESCRIPTOR_FORMAT);
    add_string(writer, &items, ".space = \"%s\"", interface->space.text);
    add_string(writer, &items, ".name = \"%s\"", interface->name.text);
    add_string(writer, &items, ".major = %s", version_number(writer, interface->version.major));
    add_string(writer, &items, ".minor = %s", version_number(writer, interface->version.minor));
    add_string(writer, &items, ".patch = %s", version_number(writer, interface->version.patch));
    if (interface->group_count > 0) {
        add_string(writer, &items, ".groups = groups_");
        add_string(writer, &items, ".group_count = %lu", (unsigned long)interface->group_count);
    }
    if (interface->object_count > 0) {
        add_string(writer, &items, ".kinds = kinds_");
        add_string(writer, &items, ".kind_count = %lu", (unsigned long)interface->object_count);
    }
    if (interface->record_count > 0) {
        add_string(writer, &items, ".records = records_");
        add_string(writer, &items, ".record_count = %lu", (unsigned long)interface->record_count);
    }
    add_string(writer, &items, ".scripts = scripts_");
    add_string(writer, &items, ".script_count = %lu", (unsigned long)script_count);
    write_initialiser(
        writer, "",
        arena_format(writer->arena, "const stasis_interface %sinterface = ", writer->prefix),
        &items, ";");
}

/* Writes an entry point's typed caller. */
static void write_typed_caller(struct writer *writer, const struct script *script,
                               const struct function *entry_point) {
    const struct function_names names = entry_point_names(writer, script, entry_point);
    const struct strings parameters = typed_caller_parameters(writer, entry_point);
    text_append(writer->out, "\n");
    write_list(writer, "",
               arena_format(writer->arena, "stasis_status %s%s(", writer->prefix, names.base),
               &parameters, ") {");
    if (entry_point->parameters != NULL) {
        text_format(writer->out, "    struct %s_arguments_ arguments_;\n", names.base);
        for (const struct parameter *parameter = entry_point->parameters; parameter != NULL;
             parameter = parameter->next) {
            const char *name = c_member_name(parameter->name.text, writer->arena);
            text_format(writer->out, "    arguments_.%s = %s;\n", name, name);
        }
    }
    struct strings arguments = {0};
    add_string(writer, &arguments, "instance");
    add_string(writer, &arguments, "&%sinterface", writer->prefix);
    add_string(writer, &arguments, "%s%s_ENTRY", writer->macro_prefix,
               c_uppercase(names.base, writer->arena));
    add_string(writer, &arguments, "%s", entry_point->parameters != NULL ? "&arguments_" : "NULL");
    add_string(writer, &arguments, "%s", entry_point->has_result ? "result" : "NULL");
    write_list(writer, "    ", "return stasis_call(", &arguments, ");");
    text_append(writer->out, "}\n");
}

/* Writes the source. */
static void write_source(struct writer *writer, const char *base, const char *input) {
    write_banner(writer, input);
    text_format(writer->out,
                "\n#include \"%s.h\"\n\n#include <stasis_interface.h>\n\n"
                "#include <stddef.h>\n",
                base);
    for (const struct group *group = writer->interface->groups; group != NULL;
         group = group->next) {
        write_group_source(writer, group);
    }
    if (writer->interface->record_count > 0) {
        write_records(writer);
    }
    if (writer->interface->object_count > 0) {
        write_kinds(writer);
    }
    if (writer->interface->group_count > 0) {
        write_groups(writer);
    }
    for (const struct script *script = writer->interface->scripts; script != NULL;
         script = script->next) {
        write_script_source(writer, script);
    }
    write_interface_definition(writer);
    bool any = false;
    for (const struct script *script = writer->interface->scripts; script != NULL;
         script = script->next) {
        for (const struct function *entry_point = script->entry_points; entry_point != NULL;
             entry_point = entry_point->next) {
            if (!any) {
                text_append(writer->out, "\n/* The typed callers. */\n");
                any = true;
            }
            write_typed_caller(writer, script, entry_point);
        }
    }
}

void generate_host(const struct interface *interface, const char *prefix, const char *input,
                   struct arena *arena, struct output_file files[HOST_FILE_COUNT]) {
    struct writer writer;
    memset(&writer, 0, sizeof writer);
    writer.arena = arena;
    writer.interface = interface;
    writer.prefix = prefix;
    writer.macro_prefix = c_uppercase(prefix, arena);
    order_records(&writer);
    order_groups(&writer);
    const char *base = arena_copy(arena, prefix, strlen(prefix) - 1);

    struct text header;
    text_init(&header, arena);
    writer.out = &header;
    write_header(&writer, base, input);
    files[0].name = arena_format(arena, "%s.h", base);
    files[0].contents = header.data;
    files[0].size = header.length;

    struct text source;
    text_init(&source, arena);
    writer.out = &source;
    write_source(&writer, base, input);
    files[1].name = arena_format(arena, "%s.c", base);
    files[1].contents = source.data;
    files[1].size = source.length;
}
