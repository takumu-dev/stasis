#include "c_writer.h"

#include "arena.h"
#include "c_names.h"
#include "interface.h"
#include "text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

void add_string(struct writer *writer, struct strings *strings, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    strings_add(strings, writer->arena, arena_vformat(writer->arena, format, arguments));
    va_end(arguments);
}

void write_list(struct writer *writer, const char *indent, const char *head,
                const struct strings *items, const char *tail) {
    const size_t joined = joined_length(items);
    const size_t limit = LINE_LIMIT - writer->margin;
    if (strlen(indent) + strlen(head) + joined + strlen(tail) <= limit) {
        text_format(writer->out, "%s%s", indent, head);
        text_append_joined(writer->out, items);
        text_format(writer->out, "%s\n", tail);
        return;
    }
    text_format(writer->out, "%s%s\n", indent, head);
    if (strlen(indent) + 4 + joined + strlen(tail) <= limit) {
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

void write_doc(struct writer *writer, const char *indent, const char *first, const struct doc *doc,
               const char *paragraph) {
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
        const size_t length = end == NULL ? strlen(first) : (size_t)(end - first);
        text_fill(writer->out, arena_format(writer->arena, "%s *", indent),
                  arena_copy(writer->arena, first, length));
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

/* The C type of each scalar type. */
static const char *const scalar_types[] = {
    [TYPE_U8] = "uint8_t",   [TYPE_U16] = "uint16_t", [TYPE_U32] = "uint32_t",
    [TYPE_U64] = "uint64_t", [TYPE_S8] = "int8_t",    [TYPE_S16] = "int16_t",
    [TYPE_S32] = "int32_t",  [TYPE_S64] = "int64_t",  [TYPE_F32] = "float",
    [TYPE_F64] = "double",   [TYPE_BOOL] = "bool",
};

const char *c_type(struct writer *writer, const struct type *type) {
    switch (type->kind) {
    case TYPE_STRING:
        if (writer->bindings) {
            return arena_format(writer->arena, "%sstring", writer->prefix);
        }
        return "stasis_string";
    case TYPE_RECORD:
        return arena_format(writer->arena, "%s%s", writer->prefix,
                            c_record_name(type->record, writer->arena));
    case TYPE_OBJECT:
        return arena_format(writer->arena, "%s%s%s", writer->prefix,
                            c_object_name(type->object, writer->arena),
                            writer->bindings ? "" : " *");
    default:
        return scalar_types[type->kind];
    }
}

const char *pointer_to(struct writer *writer, const char *type) {
    return arena_format(writer->arena, "%s%s*", type, type[strlen(type) - 1] == '*' ? "" : " ");
}

const char *declaration(struct writer *writer, const char *type, const char *name) {
    return arena_format(writer->arena, "%s%s%s", type, type[strlen(type) - 1] == '*' ? "" : " ",
                        name);
}

struct type handle_to(const struct object *object) {
    struct type type;
    memset(&type, 0, sizeof type);
    type.kind = TYPE_OBJECT;
    type.object = object;
    return type;
}

const char *function_word(const struct function *function) {
    return function->object != NULL ? "method" : "host function";
}

bool holds_string(const struct writer *writer, const struct type *type) {
    if (type->kind == TYPE_RECORD) {
        return writer->contents.strings[type->record->index];
    }
    return type->kind == TYPE_STRING;
}

bool holds_handle(const struct writer *writer, const struct type *type) {
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

void writer_init(struct writer *writer, const struct interface *interface, const char *prefix,
                 bool bindings, struct arena *arena) {
    memset(writer, 0, sizeof *writer);
    writer->arena = arena;
    writer->interface = interface;
    writer->prefix = prefix;
    writer->macro_prefix = c_uppercase(prefix, arena);
    writer->bindings = bindings;
    order_records(writer);
    order_groups(writer);
}

struct function_names function_names(struct writer *writer, const struct group *group,
                                     const struct function *function) {
    struct function_names names;
    names.function = function;
    names.qualified = arena_format(writer->arena, "%s.%s", group->name.text, function->name.text);
    names.base = c_function_name(group, function, writer->arena);
    return names;
}

struct function_names method_names(struct writer *writer, const struct function *method) {
    struct function_names names;
    names.function = method;
    names.qualified = arena_format(writer->arena, "%s.%s.%s", method->object->group->name.text,
                                   method->object->name.text, method->name.text);
    names.base = c_method_name(method, writer->arena);
    return names;
}

struct function_names entry_point_names(struct writer *writer, const struct script *script,
                                        const struct function *entry_point) {
    struct function_names names;
    names.function = entry_point;
    names.qualified =
        arena_format(writer->arena, "%s.%s", script->name.text, entry_point->name.text);
    names.base = c_entry_point_name(script, entry_point, writer->arena);
    return names;
}

const char *interface_title(const struct writer *writer) {
    const struct interface *interface = writer->interface;
    return arena_format(writer->arena, "%s:%s@%lu.%lu.%lu", interface->space.text,
                        interface->name.text, (unsigned long)interface->version.major,
                        (unsigned long)interface->version.minor,
                        (unsigned long)interface->version.patch);
}

void write_banner(struct writer *writer, const char *input) {
    text_format(writer->out, "/* Generated by stasis-gen %s from %s. Do not edit. */\n",
                STASIS_VERSION, input);
}

void write_record_declaration(struct writer *writer, const struct record *record) {
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

void add_parameter_declarations(struct writer *writer, struct strings *declarations,
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

void write_section_start(struct writer *writer, const char *name, const struct doc *doc) {
    text_append(writer->out, "\n");
    write_doc(writer, "", name, doc, NULL);
    text_append(writer->out, "/** @{ */\n");
}

void write_section_end(struct writer *writer) { text_append(writer->out, "\n/** @} */\n"); }
