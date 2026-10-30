#include "host.h"

#include "arena.h"
#include "c_names.h"
#include "c_writer.h"
#include "files.h"
#include "interface.h"
#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The descriptor format that the generated data has, which is stasis_interface.h's
 * STASIS_INTERFACE_FORMAT. It's written as a number, so that the data names the format it was
 * generated in even when it's compiled against a later stasis_interface.h. */
enum { DESCRIPTOR_FORMAT = 1 };

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

/* The descriptor's name for each built-in type. */
static const char *const descriptor_types[] = {
    [TYPE_U8] = "STASIS_TYPE_U8",     [TYPE_U16] = "STASIS_TYPE_U16",
    [TYPE_U32] = "STASIS_TYPE_U32",   [TYPE_U64] = "STASIS_TYPE_U64",
    [TYPE_S8] = "STASIS_TYPE_S8",     [TYPE_S16] = "STASIS_TYPE_S16",
    [TYPE_S32] = "STASIS_TYPE_S32",   [TYPE_S64] = "STASIS_TYPE_S64",
    [TYPE_F32] = "STASIS_TYPE_F32",   [TYPE_F64] = "STASIS_TYPE_F64",
    [TYPE_BOOL] = "STASIS_TYPE_BOOL", [TYPE_STRING] = "STASIS_TYPE_STRING",
};

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

/* The stack that a host function has free when a script calls it, as text such as `16 KiB`. */
static const char *host_function_stack(struct writer *writer) {
    const unsigned long size = STASIS_HOST_FUNCTION_STACK;
    if (size % 1024 == 0) {
        return arena_format(writer->arena, "%lu KiB", size / 1024);
    }
    return arena_format(writer->arena, "%lu bytes", size);
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
        add_string(writer, items, ".type = %s", descriptor_types[type->kind]);
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
    writer_init(&writer, interface, prefix, false, arena);
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
