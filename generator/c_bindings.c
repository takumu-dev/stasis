#include "c_bindings.h"

#include "arena.h"
#include "c_names.h"
#include "c_writer.h"
#include "files.h"
#include "interface.h"
#include "notices.h"
#include "signatures.h"
#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The most core values that a function's parameters flatten to, as the canonical ABI has it, past
 * which they spill into memory and the function takes their address. */
enum { MAX_FLAT_PARAMETERS = 16 };

/* The longest string literal that C99 and C17 require a compiler to take, which each block of
 * assembly that holds the notices stays within. */
enum { STRING_LITERAL_LIMIT = 4095 };

/* Whether `script` uses `group`, which makes its host functions and methods the script's to
 * call. */
static bool uses(const struct script *script, const struct group *group) {
    for (const struct script_use *use = script->uses; use != NULL; use = use->next) {
        if (use->group == group) {
            return true;
        }
    }
    return false;
}

/* Whether `type` is a scalar: neither a string nor a record nor a handle. */
static bool is_scalar(const struct type *type) {
    switch (type->kind) {
    case TYPE_STRING:
    case TYPE_RECORD:
    case TYPE_OBJECT:
        return false;
    default:
        return true;
    }
}

/* A value that isn't a record, in a value that may be one. */
struct leaf {
    const struct type *type;
    /* The path to it from the value, as in `.at.x`, which is empty when the value is the leaf. */
    const char *path;
    /* The number of records that start just before it, and that end just after it, which an
     * initialiser's braces open and close around it. */
    size_t opened;
    size_t closed;
};

/* The leaves of a value, in order. */
struct leaves {
    struct leaf *items;
    size_t count;
    size_t capacity;
};

/* Adds a leaf to a list, which grows in `arena`. */
static void add_leaf(struct leaves *leaves, struct arena *arena, const struct type *type,
                     const char *path, size_t opened) {
    leaves->items =
        arena_grow(arena, leaves->items, leaves->count, &leaves->capacity, sizeof *leaves->items);
    struct leaf *leaf = &leaves->items[leaves->count++];
    leaf->type = type;
    leaf->path = path;
    leaf->opened = opened;
    leaf->closed = 0;
}

/* A record whose fields are being walked: the field that comes next, and the path to the record. */
struct leaf_frame {
    const struct field *next;
    const char *path;
};

/* The leaves of a value of `type`. A record's fields are walked in turn, and those of the records
 * they hold within theirs, keeping the field that comes next in each record on a stack. */
static struct leaves leaves_of(struct writer *writer, const struct type *type) {
    struct arena *arena = writer->arena;
    struct leaves leaves = {NULL, 0, 0};
    if (type->kind != TYPE_RECORD) {
        add_leaf(&leaves, arena, type, "", 0);
        return leaves;
    }
    size_t capacity = 0;
    struct leaf_frame *frames = arena_grow(arena, NULL, 0, &capacity, sizeof *frames);
    frames[0].next = type->record->fields;
    frames[0].path = "";
    size_t depth = 1;
    size_t opened = 1;
    while (depth > 0) {
        struct leaf_frame *frame = &frames[depth - 1];
        const struct field *field = frame->next;
        if (field == NULL) {
            /* A record holds at least one field, so a leaf comes before its end. */
            if (leaves.count > 0) {
                leaves.items[leaves.count - 1].closed++;
            }
            depth--;
            continue;
        }
        frame->next = field->next;
        const char *path =
            arena_format(arena, "%s.%s", frame->path, c_member_name(field->name.text, arena));
        if (field->type.kind != TYPE_RECORD) {
            add_leaf(&leaves, arena, &field->type, path, opened);
            opened = 0;
            continue;
        }
        frames = arena_grow(arena, frames, depth, &capacity, sizeof *frames);
        frames[depth].next = field->type.record->fields;
        frames[depth].path = path;
        depth++;
        opened++;
    }
    return leaves;
}

/* The number of core values that a value of `type` flattens to: two for each string in it, and one
 * for each scalar and handle. */
static size_t flat_count(struct writer *writer, const struct type *type) {
    const struct leaves leaves = leaves_of(writer, type);
    size_t count = 0;
    for (size_t i = 0; i < leaves.count; i++) {
        count += leaves.items[i].type->kind == TYPE_STRING ? 2 : 1;
    }
    return count;
}

/* The number of core values that a function's parameters flatten to. */
static size_t parameters_flat_count(struct writer *writer, const struct function *function) {
    size_t count = function->object != NULL ? 1 : 0;
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        count += flat_count(writer, &parameter->type);
    }
    return count;
}

/* Whether a function's parameters spill into memory. */
static bool spills(struct writer *writer, const struct function *function) {
    return parameters_flat_count(writer, function) > MAX_FLAT_PARAMETERS;
}

/* Whether a function's result is in a return area, since it flattens to more than one core
 * value. */
static bool returns_area(struct writer *writer, const struct function *function) {
    if (!function->has_result) {
        return false;
    }
    return flat_count(writer, &function->result) > 1;
}

/* Whether a function returns a value that holds a string. */
static bool returns_string(const struct writer *writer, const struct function *function) {
    if (!function->has_result) {
        return false;
    }
    return holds_string(writer, &function->result);
}

/* Whether any of a function's parameters holds a string. */
static bool parameters_hold_string(const struct writer *writer, const struct function *function) {
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        if (holds_string(writer, &parameter->type)) {
            return true;
        }
    }
    return false;
}

/* The C type of a scalar's or a handle's core value, as an import or an export takes it: a
 * scalar's own, or a handle's value. */
static const char *core_type(struct writer *writer, const struct type *type) {
    return type->kind == TYPE_OBJECT ? "uint32_t" : c_type(writer, type);
}

/* The C type of the one core value that a value of `type` flattens to, as core_type() gives it. */
static const char *single_core_type(struct writer *writer, const struct type *type) {
    while (type->kind == TYPE_RECORD) {
        type = &type->record->fields->type;
    }
    return core_type(writer, type);
}

/* Adds the C type of each core value that a value of `type` flattens to, as core_type() gives it,
 * and a string's data and size, whose data is `const` when `import` is set. */
static void add_core_types(struct writer *writer, struct strings *types, const struct type *type,
                           bool import) {
    const struct leaves leaves = leaves_of(writer, type);
    for (size_t i = 0; i < leaves.count; i++) {
        const struct type *leaf = leaves.items[i].type;
        if (leaf->kind == TYPE_STRING) {
            add_string(writer, types, "%s", import ? "const char *" : "char *");
            add_string(writer, types, "size_t");
        } else {
            add_string(writer, types, "%s", core_type(writer, leaf));
        }
    }
}

/* Adds each core value that `value`, an expression of `type`, flattens to, as an expression. */
static void add_core_values(struct writer *writer, struct strings *values, const struct type *type,
                            const char *value) {
    const struct leaves leaves = leaves_of(writer, type);
    for (size_t i = 0; i < leaves.count; i++) {
        const struct leaf *leaf = &leaves.items[i];
        if (leaf->type->kind == TYPE_OBJECT) {
            add_string(writer, values, "%s%s.handle", value, leaf->path);
        } else if (leaf->type->kind == TYPE_STRING) {
            add_string(writer, values, "%s%s.data", value, leaf->path);
            add_string(writer, values, "%s%s.size", value, leaf->path);
        } else {
            add_string(writer, values, "%s%s", value, leaf->path);
        }
    }
}

/* A value of `type` made from the core values it flattens to, which are the items of `cores` from
 * `*next` on, moving `*next` past them: the core value itself for a scalar, and a compound literal
 * for anything else. */
static const char *compound(struct writer *writer, const struct type *type,
                            const struct strings *cores, size_t *next) {
    if (is_scalar(type)) {
        return cores->items[(*next)++];
    }
    const struct leaves leaves = leaves_of(writer, type);
    struct text text;
    text_init(&text, writer->arena);
    text_format(&text, "(%s)", c_type(writer, type));
    for (size_t i = 0; i < leaves.count; i++) {
        const struct leaf *leaf = &leaves.items[i];
        if (i > 0) {
            text_append(&text, ", ");
        }
        for (size_t opened = 0; opened < leaf->opened; opened++) {
            text_append(&text, "{");
        }
        if (leaf->type->kind == TYPE_OBJECT) {
            text_format(&text, "{%s}", cores->items[*next]);
            *next += 1;
        } else if (leaf->type->kind == TYPE_STRING) {
            text_format(&text, "{%s, %s}", cores->items[*next], cores->items[*next + 1]);
            *next += 2;
        } else {
            text_append(&text, cores->items[*next]);
            *next += 1;
        }
        for (size_t closed = 0; closed < leaf->closed; closed++) {
            text_append(&text, "}");
        }
    }
    return text.data;
}

/* Where the one core value that a value of `type` flattens to is in the value, after the value: a
 * path through its records to a scalar, or to a handle's value, or nothing for a scalar. */
static const char *single_core_path(struct writer *writer, const struct type *type) {
    const char *path = "";
    while (type->kind == TYPE_RECORD) {
        const struct field *field = type->record->fields;
        path = arena_format(writer->arena, "%s.%s", path,
                            c_member_name(field->name.text, writer->arena));
        type = &field->type;
    }
    return type->kind == TYPE_OBJECT ? arena_format(writer->arena, "%s.handle", path) : path;
}

/* Adds the path after a value of `type` to each string it holds. */
static void add_string_paths(struct writer *writer, struct strings *paths,
                             const struct type *type) {
    const struct leaves leaves = leaves_of(writer, type);
    for (size_t i = 0; i < leaves.count; i++) {
        if (leaves.items[i].type->kind == TYPE_STRING) {
            add_string(writer, paths, "%s", leaves.items[i].path);
        }
    }
}

/* A line of the interface: its major version, or `0.<minor>` under 0.x. */
static const char *interface_line(const struct writer *writer) {
    const struct version *version = &writer->interface->version;
    if (version->major > 0) {
        return arena_format(writer->arena, "%lu", (unsigned long)version->major);
    }
    return arena_format(writer->arena, "0.%lu", (unsigned long)version->minor);
}

/* The hash of a function's signature. */
static const char *signature_hash(struct writer *writer, const struct function *function) {
    struct signature signature;
    encode_signature(function, writer->arena, &signature);
    return arena_copy(writer->arena, signature.hash, strlen(signature.hash));
}

/* The export name of an entry point, `<namespace>:<name>/<kind>@<line>#<entry point>:<hash>`. */
static const char *export_name(struct writer *writer, const struct script *script,
                               const struct function *entry_point) {
    const struct interface *interface = writer->interface;
    return arena_format(writer->arena, "%s:%s/%s@%s#%s:%s", interface->space.text,
                        interface->name.text, script->name.text, interface_line(writer),
                        entry_point->name.text, signature_hash(writer, entry_point));
}

/* Writes the string type, and the prototype of its helper. */
static void write_string_declarations(struct writer *writer) {
    const char *prefix = writer->prefix;
    text_append(writer->out, "\n");
    write_doc(writer, "",
              "@brief A string of UTF-8: `size` bytes at `data`, which needn't end in a null.",
              NULL, NULL);
    text_format(writer->out,
                "typedef struct %sstring {\n    char *data;\n    size_t size;\n} %sstring;\n",
                prefix, prefix);
    text_append(writer->out, "\n");
    write_doc(writer, "",
              "@brief The string of the C string `text`, which points at `text` rather than "
              "copying it.",
              NULL, NULL);
    text_format(writer->out, "%sstring %sstring_of(const char *text);\n", prefix, prefix);
}

/* Writes a kind of host object's handle struct. */
static void write_handle_declaration(struct writer *writer, const struct object *object) {
    const char *name =
        arena_format(writer->arena, "%s%s", writer->prefix, c_object_name(object, writer->arena));
    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena, "@brief A handle to a host object of the kind `%s.%s`.",
                           object->group->name.text, object->name.text),
              &object->doc,
              "Two handles name the same host object exactly when their `handle`s are equal, so a "
              "script can compare them, and key a table with them. A script gets its handles from "
              "the host alone: passing a host function a zeroed handle, or any other that the host "
              "hasn't given the instance, traps.");
    text_format(writer->out, "typedef struct %s {\n    uint32_t handle;\n} %s;\n", name, name);
}

/* Writes the prototype of a record's `_free` helper. */
static void write_free_declaration(struct writer *writer, const struct record *record) {
    const char *name =
        arena_format(writer->arena, "%s%s", writer->prefix, c_record_name(record, writer->arena));
    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena,
                           "@brief Frees the strings in a `%s.%s` that the script owns.",
                           record->group->name.text, record->name.text),
              NULL, "The script owns those in a record that a host function returns.");
    text_format(writer->out, "void %s_free(%s *record);\n", name, name);
}

/* What the documentation of a host function or a method says beyond the interface's doc
 * comment. */
static const char *import_rules(struct writer *writer, const struct function *function) {
    struct text rules;
    text_init(&rules, writer->arena);
    if (function->object != NULL) {
        text_append(&rules, " `self` names the host object it's called on.");
    }
    if (parameters_hold_string(writer, function)) {
        text_append(&rules, " The strings among its arguments stay the script's.");
    }
    if (function->has_result && function->result.kind == TYPE_STRING) {
        text_append(&rules, " The string it returns is the script's, which frees its `data`.");
    } else if (function->has_result && holds_string(writer, &function->result)) {
        text_format(&rules,
                    " The strings in the record it returns are the script's, which frees them "
                    "with %s%s_free().",
                    writer->prefix, c_record_name(function->result.record, writer->arena));
    }
    /* Each rule starts with a space. */
    return rules.length > 0 ? rules.data + 1 : rules.data;
}

/* The C declarations of a function's parameters. */
static struct strings parameter_declarations(struct writer *writer,
                                             const struct function *function) {
    struct strings parameters = {0};
    add_parameter_declarations(writer, &parameters, function);
    return parameters;
}

/* The C type of a function's result, or `void` if it has none. */
static const char *result_type(struct writer *writer, const struct function *function) {
    return function->has_result ? c_type(writer, &function->result) : "void";
}

/* Writes a function's prototype, `<result> <name>(<parameters>)`, with `void` for parameters when
 * there are none, and `tail` after it, such as `);` or `) {`. */
static void write_prototype(struct writer *writer, const char *result, const char *name,
                            const struct strings *parameters, const char *tail) {
    struct strings none = {0};
    if (parameters->count == 0) {
        add_string(writer, &none, "void");
        parameters = &none;
    }
    write_list(writer, "", arena_format(writer->arena, "%s(", declaration(writer, result, name)),
               parameters, tail);
}

/* The C name of a function's own C name after the prefix, `base`, with `suffix` after it, as in
 * `acme_dungeon_npc_tick_export_`. */
static const char *prefixed(struct writer *writer, const char *base, const char *suffix) {
    return arena_format(writer->arena, "%s%s%s", writer->prefix, base, suffix);
}

/* Writes the prototype of the function through which a script calls a host function or a
 * method. */
static void write_import_declaration(struct writer *writer, const struct function_names *names) {
    const struct function *function = names->function;
    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena, "@brief Calls the %s `%s`.", function_word(function),
                           names->qualified),
              &function->doc, import_rules(writer, function));
    const struct strings parameters = parameter_declarations(writer, function);
    write_prototype(writer, result_type(writer, function), prefixed(writer, names->base, ""),
                    &parameters, ");");
}

/* Writes the header's declarations for a group, in a member group of their own: its handle
 * structs, its records, and, if the kind of script uses it, its host functions and methods. */
static void write_group_declarations(struct writer *writer, const struct script *script,
                                     const struct group *group) {
    const bool used = uses(script, group);
    bool any = false;
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind == ITEM_RECORD || item->kind == ITEM_OBJECT ||
            (used && item->kind == ITEM_FUNCTION)) {
            any = true;
        }
    }
    if (!any) {
        return;
    }
    write_section_start(writer, arena_format(writer->arena, "@name The group %s", group->name.text),
                        &group->doc);
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind == ITEM_OBJECT) {
            write_handle_declaration(writer, item->as.object);
        }
    }
    for (size_t i = 0; i < writer->interface->record_count; i++) {
        const struct record *record = writer->records[i];
        if (record->group == group) {
            write_record_declaration(writer, record);
            if (writer->contents.strings[record->index]) {
                write_free_declaration(writer, record);
            }
        }
    }
    for (const struct item *item = used ? group->items : NULL; item != NULL; item = item->next) {
        if (item->kind == ITEM_FUNCTION) {
            const struct function_names names = function_names(writer, group, item->as.function);
            write_import_declaration(writer, &names);
        } else if (item->kind == ITEM_OBJECT) {
            for (const struct function *method = item->as.object->methods; method != NULL;
                 method = method->next) {
                const struct function_names names = method_names(writer, method);
                write_import_declaration(writer, &names);
            }
        }
    }
    write_section_end(writer);
}

/* The name of the macro through which a script defines an optional entry point. */
static const char *entry_point_macro(struct writer *writer, const struct function_names *names) {
    return arena_format(writer->arena, "%s%s", writer->macro_prefix,
                        c_uppercase(names->base, writer->arena));
}

/* What the documentation of an entry point says beyond the interface's doc comment. */
static const char *entry_point_rules(struct writer *writer, const struct function_names *names) {
    const struct function *entry_point = names->function;
    struct text rules;
    text_init(&rules, writer->arena);
    if (entry_point->optional) {
        text_format(&rules, " A script that provides it defines it with %s.",
                    entry_point_macro(writer, names));
    }
    if (parameters_hold_string(writer, entry_point)) {
        text_append(&rules, " Its arguments are borrowed: the strings in them are freed once it "
                            "returns, so it copies any it keeps.");
    }
    if (entry_point->has_result && holds_string(writer, &entry_point->result)) {
        text_append(&rules, " What it returns is copied, so it may return a string literal, or a "
                            "string it keeps.");
    }
    return rules.length > 0 ? rules.data + 1 : rules.data;
}

/* The C type that an entry point's export returns: the one core value its result flattens to, the
 * address of its return area, or nothing. */
static const char *export_result_type(struct writer *writer, const struct function *entry_point) {
    if (!entry_point->has_result) {
        return "void";
    }
    if (returns_area(writer, entry_point)) {
        return pointer_to(writer, c_type(writer, &entry_point->result));
    }
    return single_core_type(writer, &entry_point->result);
}

/* The C types of the core values that an entry point's export takes: those its parameters flatten
 * to, or their address once they spill. */
static struct strings export_parameter_types(struct writer *writer,
                                             const struct function *entry_point) {
    struct strings types = {0};
    if (spills(writer, entry_point)) {
        add_string(writer, &types, "void *");
        return types;
    }
    for (const struct parameter *parameter = entry_point->parameters; parameter != NULL;
         parameter = parameter->next) {
        add_core_types(writer, &types, &parameter->type, false);
    }
    return types;
}

/* Writes the attributed prototype of an entry point's export, `<base>_export_`. */
static void write_export_declaration(struct writer *writer, const struct script *script,
                                     const struct function_names *names) {
    const struct function *entry_point = names->function;
    text_format(writer->out, "__attribute__((export_name(\"%s\")))\n",
                export_name(writer, script, entry_point));
    const struct strings types = export_parameter_types(writer, entry_point);
    write_prototype(writer, export_result_type(writer, entry_point),
                    prefixed(writer, names->base, "_export_"), &types, ");");
}

/* Writes the prototype of the function that puts an entry point's result where Stasis reads it
 * from, `<base>_return_`, which returns the address of its return area, after `storage` and before
 * `tail`. */
static void write_return_prototype(struct writer *writer, const struct function_names *names,
                                   const char *storage, const char *tail) {
    const char *type = c_type(writer, &names->function->result);
    struct strings parameters = {0};
    add_string(writer, &parameters, "%s", declaration(writer, type, "result"));
    write_prototype(writer, arena_format(writer->arena, "%s%s", storage, pointer_to(writer, type)),
                    prefixed(writer, names->base, "_return_"), &parameters, tail);
}

/* The statement that frees the strings in `value`, an expression of `type` that the script owns,
 * or null if it holds none: free() on a string's data, and a record's `_free` helper. */
static const char *free_value(struct writer *writer, const struct type *type, const char *value) {
    if (type->kind == TYPE_STRING) {
        return arena_format(writer->arena, "free(%s.data);", value);
    }
    if (!holds_string(writer, type)) {
        return NULL;
    }
    return arena_format(writer->arena, "%s%s_free(&%s);", writer->prefix,
                        c_record_name(type->record, writer->arena), value);
}

/* Adds what frees each string in a value of `type`, which flattens to the core values in `cores`
 * from `*next` on, by its data, and moves `*next` past them. */
static void add_string_frees(struct writer *writer, struct strings *frees, const struct type *type,
                             const struct strings *cores, size_t *next) {
    const struct leaves leaves = leaves_of(writer, type);
    for (size_t i = 0; i < leaves.count; i++) {
        if (leaves.items[i].type->kind != TYPE_STRING) {
            *next += 1;
            continue;
        }
        add_string(writer, frees, "free(%s);", cores->items[*next]);
        *next += 2;
    }
}

/* Writes the definition of an entry point's export, which lifts its arguments from the core values
 * Stasis passes, calls the script's function, frees the strings in its arguments, and returns its
 * result as Stasis lifts it. */
static void write_export_definition(struct writer *writer, const struct function_names *names) {
    const struct function *entry_point = names->function;
    const struct strings types = export_parameter_types(writer, entry_point);
    /* The core values, as the export's parameters name them. */
    struct strings cores = {0};
    struct strings parameters = {0};
    for (size_t i = 0; i < types.count; i++) {
        add_string(writer, &cores, "p%lu_", (unsigned long)i);
        add_string(writer, &parameters, "%s",
                   declaration(writer, types.items[i], cores.items[cores.count - 1]));
    }
    text_append(writer->out, "\n");
    write_prototype(writer, export_result_type(writer, entry_point),
                    prefixed(writer, names->base, "_export_"), &parameters, ") {");

    /* The arguments of the script's function, and what frees the strings in them. */
    struct strings arguments = {0};
    struct strings frees = {0};
    if (spills(writer, entry_point)) {
        text_append(writer->out, "    struct {\n");
        for (const struct parameter *parameter = entry_point->parameters; parameter != NULL;
             parameter = parameter->next) {
            const char *member = c_member_name(parameter->name.text, writer->arena);
            text_format(writer->out, "        %s;\n",
                        declaration(writer, c_type(writer, &parameter->type), member));
            const char *argument = arena_format(writer->arena, "arguments_->%s", member);
            strings_add(&arguments, writer->arena, argument);
            const char *freeing = free_value(writer, &parameter->type, argument);
            if (freeing != NULL) {
                strings_add(&frees, writer->arena, freeing);
            }
        }
        text_append(writer->out, "    } *arguments_ = p0_;\n");
        add_string(writer, &frees, "free(arguments_);");
    } else {
        size_t next = 0;
        size_t freed = 0;
        for (const struct parameter *parameter = entry_point->parameters; parameter != NULL;
             parameter = parameter->next) {
            strings_add(&arguments, writer->arena,
                        compound(writer, &parameter->type, &cores, &next));
            add_string_frees(writer, &frees, &parameter->type, &cores, &freed);
        }
    }

    const char *function = arena_format(writer->arena, "%s%s", writer->prefix, names->base);
    const char *return_area = arena_format(writer->arena, "%s_return_", function);
    const bool area = returns_area(writer, entry_point);
    if (!entry_point->has_result) {
        write_list(writer, "    ", arena_format(writer->arena, "%s(", function), &arguments, ");");
    } else if (frees.count == 0 && area) {
        write_list(writer, "    ",
                   arena_format(writer->arena, "return %s(%s(", return_area, function), &arguments,
                   "));");
    } else if (frees.count == 0) {
        write_list(
            writer, "    ", arena_format(writer->arena, "return %s(", function), &arguments,
            arena_format(writer->arena, ")%s;", single_core_path(writer, &entry_point->result)));
    } else if (area) {
        /* The result may hold the strings of the arguments, so it's copied before they're
         * freed. */
        write_list(writer, "    ",
                   arena_format(
                       writer->arena, "%s = %s(%s(",
                       declaration(writer, pointer_to(writer, c_type(writer, &entry_point->result)),
                                   "area_"),
                       return_area, function),
                   &arguments, "));");
    } else {
        write_list(
            writer, "    ",
            arena_format(writer->arena, "%s = %s(",
                         declaration(writer, c_type(writer, &entry_point->result), "result_"),
                         function),
            &arguments, ");");
    }
    for (size_t i = 0; i < frees.count; i++) {
        text_format(writer->out, "    %s\n", frees.items[i]);
    }
    if (entry_point->has_result && frees.count > 0) {
        if (area) {
            text_append(writer->out, "    return area_;\n");
        } else {
            text_format(writer->out, "    return result_%s;\n",
                        single_core_path(writer, &entry_point->result));
        }
    }
    text_append(writer->out, "}\n");
}

/* Writes the prototype of an entry point's cabi_post_*, `<base>_post_`, which frees the strings its
 * result holds once Stasis has read them, before `tail`. */
static void write_post_prototype(struct writer *writer, const struct function_names *names,
                                 const char *tail) {
    struct strings parameters = {0};
    add_string(
        writer, &parameters, "%s",
        declaration(writer, pointer_to(writer, c_type(writer, &names->function->result)), "area"));
    write_prototype(writer, "void", prefixed(writer, names->base, "_post_"), &parameters, tail);
}

/* Writes the attributed prototype of an entry point's cabi_post_*. */
static void write_post_declaration(struct writer *writer, const struct script *script,
                                   const struct function_names *names) {
    text_format(writer->out, "__attribute__((export_name(\"cabi_post_%s\")))\n",
                export_name(writer, script, names->function));
    write_post_prototype(writer, names, ");");
}

/* Writes the definition of an entry point's cabi_post_*. */
static void write_post_definition(struct writer *writer, const struct function_names *names) {
    const struct type *type = &names->function->result;
    text_append(writer->out, "\n");
    write_post_prototype(writer, names, ") {");
    if (type->kind == TYPE_STRING) {
        text_append(writer->out, "    free(area->data);\n");
    } else {
        text_format(writer->out, "    %s%s_free(area);\n", writer->prefix,
                    c_record_name(type->record, writer->arena));
    }
    text_append(writer->out, "}\n");
}

/* Writes `text`, which holds lines that each end in a newline, as the lines of a macro's
 * definition, 4 columns in and each with a `\` after it, at the limit where it fits. Empty lines
 * are left out. */
static void write_macro_lines(struct writer *writer, const char *text) {
    while (*text != '\0') {
        const char *end = strchr(text, '\n');
        const size_t length = (size_t)(end - text);
        if (length == 0) {
            text = end + 1;
            continue;
        }
        text_format(writer->out, "    %.*s", (int)length, text);
        for (size_t column = 4 + length; column + 2 < LINE_LIMIT; column++) {
            text_append(writer->out, " ");
        }
        text_append(writer->out, " \\\n");
        text = end + 1;
    }
}

/* Writes the header's declarations for an optional entry point, after its prototype: its macro,
 * and what the macro expands to needs, which is its export's prototype and, if its result is in a
 * return area, the prototype of the function that puts the result there. */
static void write_optional_declarations(struct writer *writer, const struct script *script,
                                        const struct function_names *names) {
    const struct function *entry_point = names->function;
    const char *macro = entry_point_macro(writer, names);
    const bool post = returns_string(writer, entry_point);
    const struct strings parameters = parameter_declarations(writer, entry_point);
    struct text example;
    text_init(&example, writer->arena);
    text_format(&example, "%s(", macro);
    if (parameters.count == 0) {
        text_append(&example, "void");
    }
    text_append_joined(&example, &parameters);
    text_append(&example, ") { ... }");
    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena,
                           "@brief Defines the optional entry point `%s`, and "
                           "exports it.",
                           names->qualified),
              NULL,
              arena_format(writer->arena,
                           "It takes the place of the function's return type and name, before its "
                           "parameters and body, as in `%s`. It expands to the entry point's "
                           "export%s, and then to the start of the definition of %s%s().",
                           example.data, post ? " and its cabi_post_*" : "", writer->prefix,
                           names->base));

    struct text *out = writer->out;
    struct text wrapper;
    text_init(&wrapper, writer->arena);
    writer->out = &wrapper;
    writer->margin = strlen("    ") + strlen(" \\");
    if (post) {
        write_post_definition(writer, names);
    }
    write_export_definition(writer, names);
    writer->margin = 0;
    writer->out = out;
    text_format(writer->out, "#define %s", macro);
    for (size_t column = strlen("#define ") + strlen(macro); column + 2 < LINE_LIMIT; column++) {
        text_append(writer->out, " ");
    }
    text_append(writer->out, " \\\n");
    write_macro_lines(writer, wrapper.data);
    text_format(
        writer->out, "    %s\n",
        declaration(writer, result_type(writer, entry_point), prefixed(writer, names->base, "")));

    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena, "@brief The export of the optional entry point `%s`.",
                           names->qualified),
              NULL, arena_format(writer->arena, "%s defines it.", macro));
    write_export_declaration(writer, script, names);
    if (post) {
        text_append(writer->out, "\n");
        write_doc(writer, "",
                  arena_format(writer->arena,
                               "@brief The cabi_post_* of the optional entry point `%s`.",
                               names->qualified),
                  NULL, arena_format(writer->arena, "%s defines it.", macro));
        write_post_declaration(writer, script, names);
    }
    if (returns_area(writer, entry_point)) {
        text_append(writer->out, "\n");
        write_doc(writer, "",
                  arena_format(writer->arena,
                               "@brief Puts a result of `%s` where Stasis reads it from.",
                               names->qualified),
                  NULL, arena_format(writer->arena, "%s calls it.", macro));
        write_return_prototype(writer, names, "", ");");
    }
}

/* Writes the header's declarations for a kind of script, in a member group of their own: each
 * entry point, which the script defines, and each optional one's macro. */
static void write_script_declarations(struct writer *writer, const struct script *script) {
    if (script->entry_points == NULL) {
        return;
    }
    write_section_start(
        writer, arena_format(writer->arena, "@name The kind of script %s", script->name.text),
        &script->doc);
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        const struct function_names names = entry_point_names(writer, script, entry_point);
        const char *brief =
            arena_format(writer->arena, "@brief The entry point `%s`, which the script defines.",
                         names.qualified);
        if (entry_point->optional) {
            brief = arena_format(writer->arena, "@brief The optional entry point `%s`.",
                                 names.qualified);
        }
        text_append(writer->out, "\n");
        write_doc(writer, "", brief, &entry_point->doc, entry_point_rules(writer, &names));
        const struct strings parameters = parameter_declarations(writer, entry_point);
        write_prototype(writer, result_type(writer, entry_point), prefixed(writer, names.base, ""),
                        &parameters, ");");
        if (entry_point->optional) {
            write_optional_declarations(writer, script, &names);
        }
    }
    write_section_end(writer);
}

/* Writes the header of a kind of script's bindings, `<base>.h`. */
static void write_header(struct writer *writer, const struct script *script, const char *base,
                         const char *input) {
    const char *guard = arena_format(writer->arena, "%s_H_", c_uppercase(base, writer->arena));
    write_banner(writer, input);
    text_append(writer->out, "\n");
    write_doc(writer, "",
              arena_format(writer->arena,
                           "@file %s.h\n@brief The C bindings of the kind of script `%s`, of the "
                           "interface `%s`.",
                           base, script->name.text, interface_title(writer)),
              &writer->interface->doc,
              arena_format(writer->arena,
                           "A script of this kind includes this header, and is built with %s.c "
                           "beside its own sources. It calls host functions through the functions "
                           "declared here, and defines each required entry point declared here, "
                           "and each optional one it provides. Its constructors run once, when "
                           "Stasis starts a new instance, and never in an instance restored from "
                           "a snapshot.",
                           base));
    text_format(writer->out,
                "\n#ifndef %s\n#define %s\n\n"
                "#if !defined(__wasm32__) || !defined(__wasi__)\n"
                "#error \"%s.h is for scripts built for wasm32-wasip1\"\n#endif\n"
                "#if defined(__wasm_atomics__) || defined(_REENTRANT)\n"
                "#error \"%s.h is for scripts built without threads\"\n#endif\n\n"
                "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n"
                "#include <stdlib.h>\n",
                guard, guard, base, base);
    write_string_declarations(writer);
    for (size_t i = 0; i < writer->interface->group_count; i++) {
        write_group_declarations(writer, script, writer->groups[i]);
    }
    write_script_declarations(writer, script);
    text_append(writer->out, "\n#endif\n");
}

/* Writes a piece of a line of the notices, the `length` bytes at `piece`, as an `.ascii` directive
 * in a C string literal, with the line's newline after it if it's the line's last piece. Returns
 * the number of bytes it takes in the string literal, and the number of columns it takes in the
 * source in `columns`. It writes nothing unless `write` is set. */
static size_t write_notice_piece(struct writer *writer, const unsigned char *piece, size_t length,
                                 bool last, bool write, size_t *columns) {
    static const char start[] = "        \".ascii \\\"";
    static const char newline[] = "\\\\n";
    static const char end[] = "\\\"\\n\"";
    /* `.ascii "`, the piece, an escaped newline, and `"` and a newline. */
    size_t bytes = 8 + length + (last ? 2 : 0) + 2;
    *columns = strlen(start) + length + (last ? strlen(newline) : 0) + strlen(end);
    for (size_t i = 0; i < length; i++) {
        if (piece[i] == '"' || piece[i] == '\\') {
            /* The assembler takes `\"` and `\\`, which C escapes again. */
            bytes++;
            *columns += 3;
        }
    }
    if (!write) {
        return bytes;
    }
    text_append(writer->out, start);
    for (size_t i = 0; i < length; i++) {
        if (piece[i] == '"') {
            text_append(writer->out, "\\\\\\\"");
        } else if (piece[i] == '\\') {
            text_append(writer->out, "\\\\\\\\");
        } else {
            text_append_length(writer->out, (const char *)&piece[i], 1);
        }
    }
    text_format(writer->out, "%s%s\n", last ? newline : "", end);
    return bytes;
}

/* Writes a line of the notices, the `length` bytes at `line` without its newline, as the pieces
 * that keep it within the line limit, where it can: it's broken after a space, as late as fits.
 * Returns the number of bytes it takes in the string literal. It writes nothing unless `write` is
 * set. */
static size_t write_notice_line(struct writer *writer, const unsigned char *line, size_t length,
                                bool write) {
    size_t bytes = 0;
    size_t start = 0;
    for (;;) {
        size_t columns = 0;
        (void)write_notice_piece(writer, line + start, length - start, true, false, &columns);
        size_t cut = 0;
        for (size_t i = start + 1; i < length && columns > LINE_LIMIT; i++) {
            if (line[i - 1] != ' ') {
                continue;
            }
            size_t piece_columns = 0;
            (void)write_notice_piece(writer, line + start, i - start, false, false, &piece_columns);
            if (piece_columns <= LINE_LIMIT || cut == 0) {
                cut = i;
            }
            if (piece_columns > LINE_LIMIT) {
                break;
            }
        }
        if (cut == 0) {
            return bytes +
                   write_notice_piece(writer, line + start, length - start, true, write, &columns);
        }
        bytes += write_notice_piece(writer, line + start, cut - start, false, write, &columns);
        start = cut;
    }
}

/* Writes the notices into the custom section `notices`, through file-scope assembly, in as many
 * blocks as keep each block's string literal within the limit. */
static void write_notices(struct writer *writer) {
    static const char section[] = ".section .custom_section.notices,\"\",@\n";
    static const char text_section[] = ".text\n";
    text_append(writer->out,
                "\n/* The licence notices of wasi-libc, the C library that wasi-sdk links into "
                "every script, which\n * the script carries in a custom section of its own, "
                "outside its memory and its identity, and\n * which `llvm-strip --strip-debug` "
                "keeps. */\n");
    const unsigned char *notices = wasi_libc_notices;
    const size_t size = wasi_libc_notices_size;
    size_t block = 0;
    for (size_t start = 0; start < size;) {
        const unsigned char *end = memchr(notices + start, '\n', size - start);
        const size_t length = end == NULL ? size - start : (size_t)(end - notices) - start;
        const size_t bytes = write_notice_line(writer, notices + start, length, false);
        if (block > 0 && block + bytes > STRING_LITERAL_LIMIT) {
            text_append(writer->out, "        \".text\\n\");\n");
            block = 0;
        }
        if (block == 0) {
            text_append(writer->out,
                        "__asm__(\".section .custom_section.notices,\\\"\\\",@\\n\"\n");
            block = strlen(section) + strlen(text_section);
        }
        (void)write_notice_line(writer, notices + start, length, true);
        block += bytes;
        start += length + 1;
    }
    if (block > 0) {
        text_append(writer->out, "        \".text\\n\");\n");
    }
}

/* Whether any host function or method of `group` returns a value that holds a string. */
static bool group_returns_string(const struct writer *writer, const struct group *group) {
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind == ITEM_FUNCTION && returns_string(writer, item->as.function)) {
            return true;
        }
        const struct function *methods =
            item->kind == ITEM_OBJECT ? item->as.object->methods : NULL;
        for (const struct function *method = methods; method != NULL; method = method->next) {
            if (returns_string(writer, method)) {
                return true;
            }
        }
    }
    return false;
}

/* Whether Stasis needs the script's cabi_realloc to put values into its memory: the result of a
 * host function or a method that holds a string, among the groups the kind of script uses, or the
 * arguments of an entry point that hold a string or spill. */
static bool needs_realloc(struct writer *writer, const struct script *script) {
    for (const struct group *group = writer->interface->groups; group != NULL;
         group = group->next) {
        if (uses(script, group) && group_returns_string(writer, group)) {
            return true;
        }
    }
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        if (spills(writer, entry_point) || parameters_hold_string(writer, entry_point)) {
            return true;
        }
    }
    return false;
}

/* Writes what every script's source starts with: stasis_initialize, cabi_realloc if the kind of
 * script needs it, and the string helper. */
static void write_runtime(struct writer *writer, const struct script *script) {
    const char *prefix = writer->prefix;
    text_append(
        writer->out,
        "\n/* Runs the script's constructors, which wasm-ld gathers. */\n"
        "void __wasm_call_ctors(void);\n\n"
        "/* What Stasis calls once to start a new instance, and never in a restored one. */\n"
        "__attribute__((export_name(\"stasis_initialize\")))\n"
        "void stasis_initialize(void);\n\n"
        "void stasis_initialize(void) {\n    __wasm_call_ctors();\n}\n");
    if (needs_realloc(writer, script)) {
        text_append(
            writer->out,
            "\n/* What Stasis allocates what it puts into the script's memory with, which the "
            "script frees as\n * it frees what malloc allocates. realloc's memory is aligned "
            "as strictly as anything Stasis puts\n * there needs. */\n"
            "__attribute__((export_name(\"cabi_realloc\")))\n"
            "void *cabi_realloc(void *old, size_t old_size, size_t alignment, size_t size);\n\n"
            "void *cabi_realloc(void *old, size_t old_size, size_t alignment, size_t size) {\n"
            "    (void)old_size;\n    (void)alignment;\n    return realloc(old, size);\n}\n");
    }
    text_format(writer->out,
                "\n%sstring %sstring_of(const char *text) {\n    %sstring string;\n"
                "    /* The string never changes the text it points at. */\n"
                "    string.data = (char *)(uintptr_t)text;\n    string.size = strlen(text);\n"
                "    return string;\n}\n",
                prefix, prefix, prefix);
}

/* Writes a record's `_free` helper. */
static void write_free_definition(struct writer *writer, const struct record *record) {
    const char *name =
        arena_format(writer->arena, "%s%s", writer->prefix, c_record_name(record, writer->arena));
    text_format(writer->out, "\nvoid %s_free(%s *record) {\n", name, name);
    for (const struct field *field = record->fields; field != NULL; field = field->next) {
        const char *freeing =
            free_value(writer, &field->type,
                       arena_format(writer->arena, "record->%s",
                                    c_member_name(field->name.text, writer->arena)));
        if (freeing != NULL) {
            text_format(writer->out, "    %s\n", freeing);
        }
    }
    text_append(writer->out, "}\n");
}

/* Writes the import of a host function or a method, and the function through which a script calls
 * it, which lowers its arguments into the core values the import takes, and lifts its result. */
static void write_import_definition(struct writer *writer, const struct group *group,
                                    const struct function_names *names) {
    const struct function *function = names->function;
    const struct interface *interface = writer->interface;
    const char *field =
        arena_format(writer->arena, "%s:%s", function->name.text, signature_hash(writer, function));
    if (function->object != NULL) {
        field = arena_format(writer->arena, "%s.%s", function->object->name.text, field);
    }
    text_format(writer->out, "\n/* The %s %s. */\n\n", function_word(function), names->qualified);
    struct strings attributes = {0};
    add_string(writer, &attributes, "import_module(\"%s:%s/%s@%s\")", interface->space.text,
               interface->name.text, group->name.text, interface_line(writer));
    add_string(writer, &attributes, "import_name(\"%s\")", field);
    write_list(writer, "", "__attribute__((", &attributes, "))");

    const bool spilled = spills(writer, function);
    const bool area = returns_area(writer, function);
    struct type self;
    memset(&self, 0, sizeof self);
    if (function->object != NULL) {
        self = handle_to(function->object);
    }
    struct strings types = {0};
    struct strings values = {0};
    if (spilled) {
        add_string(writer, &types, "const void *");
        add_string(writer, &values, "&arguments_");
    } else {
        if (function->object != NULL) {
            add_core_types(writer, &types, &self, true);
            add_core_values(writer, &values, &self, "self");
        }
        for (const struct parameter *parameter = function->parameters; parameter != NULL;
             parameter = parameter->next) {
            add_core_types(writer, &types, &parameter->type, true);
            add_core_values(writer, &values, &parameter->type,
                            c_member_name(parameter->name.text, writer->arena));
        }
    }
    const char *import_result = "void";
    if (area) {
        add_string(writer, &types, "%s", pointer_to(writer, c_type(writer, &function->result)));
        add_string(writer, &values, "&returned_");
    } else if (function->has_result) {
        import_result = single_core_type(writer, &function->result);
    }
    const char *import = prefixed(writer, names->base, "_import_");
    write_prototype(writer, arena_format(writer->arena, "extern %s", import_result), import, &types,
                    ");");

    text_append(writer->out, "\n");
    const struct strings parameters = parameter_declarations(writer, function);
    write_prototype(writer, result_type(writer, function), prefixed(writer, names->base, ""),
                    &parameters, ") {");
    if (spilled) {
        struct strings members = {0};
        text_append(writer->out, "    struct {\n");
        if (function->object != NULL) {
            text_format(writer->out, "        %s;\n",
                        declaration(writer, c_type(writer, &self), "self"));
            add_string(writer, &members, "self");
        }
        for (const struct parameter *parameter = function->parameters; parameter != NULL;
             parameter = parameter->next) {
            const char *member = c_member_name(parameter->name.text, writer->arena);
            text_format(writer->out, "        %s;\n",
                        declaration(writer, c_type(writer, &parameter->type), member));
            add_string(writer, &members, "%s", member);
        }
        write_list(writer, "    ", "} arguments_ = {", &members, "};");
    }
    const char *call = arena_format(writer->arena, "%s(", import);
    if (!function->has_result) {
        write_list(writer, "    ", call, &values, ");");
    } else if (area) {
        text_format(writer->out, "    %s;\n",
                    declaration(writer, c_type(writer, &function->result), "returned_"));
        write_list(writer, "    ", call, &values, ");");
        text_append(writer->out, "    return returned_;\n");
    } else if (is_scalar(&function->result)) {
        write_list(writer, "    ", arena_format(writer->arena, "return %s", call), &values, ");");
    } else {
        text_format(writer->out, "    %s;\n",
                    declaration(writer, c_type(writer, &function->result), "returned_"));
        write_list(writer, "    ",
                   arena_format(writer->arena, "returned_%s = %s",
                                single_core_path(writer, &function->result), call),
                   &values, ");");
        text_append(writer->out, "    return returned_;\n");
    }
    text_append(writer->out, "}\n");
}

/* Whether any entry point of a kind of script returns a string, in a return area of its own. */
static bool any_entry_point_returns_string(const struct writer *writer,
                                           const struct script *script) {
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        if (returns_string(writer, entry_point)) {
            return true;
        }
    }
    return false;
}

/* Writes the function that copies a string an entry point returns. */
static void write_copy_string(struct writer *writer) {
    const char *prefix = writer->prefix;
    text_format(writer->out,
                "\n/* A copy of `string`, in memory of its own, for Stasis to read what an entry "
                "point returns from.\n * If there's no memory for it, the script aborts, which "
                "traps. */\n"
                "static %sstring copy_string_(%sstring string) {\n    %sstring copy;\n"
                "    copy.data = NULL;\n    copy.size = string.size;\n"
                "    if (string.size > 0) {\n        copy.data = malloc(string.size);\n"
                "        if (copy.data == NULL) {\n            abort();\n        }\n"
                "        memcpy(copy.data, string.data, string.size);\n    }\n"
                "    return copy;\n}\n",
                prefix, prefix, prefix);
}

/* Writes the function that puts an entry point's result where Stasis reads it from: a return area
 * of its own, with a copy of each string in it. */
static void write_return_definition(struct writer *writer, const struct function_names *names) {
    const struct type *type = &names->function->result;
    text_append(writer->out, "\n");
    write_return_prototype(writer, names, names->function->optional ? "" : "static ", ") {");
    text_format(writer->out, "    static %s;\n", declaration(writer, c_type(writer, type), "area"));
    if (type->kind == TYPE_STRING) {
        text_append(writer->out, "    area = copy_string_(result);\n");
    } else {
        text_append(writer->out, "    area = result;\n");
        struct strings paths = {0};
        add_string_paths(writer, &paths, type);
        for (size_t i = 0; i < paths.count; i++) {
            text_format(writer->out, "    area%s = copy_string_(result%s);\n", paths.items[i],
                        paths.items[i]);
        }
    }
    text_append(writer->out, "    return &area;\n}\n");
}

/* Writes the source's part of an entry point: the function that puts its result in a return area,
 * if it needs one, and for a required entry point, its export, and its cabi_post_* if its result
 * holds a string. An optional entry point's macro holds the rest of its part. */
static void write_entry_point_source(struct writer *writer, const struct script *script,
                                     const struct function *entry_point) {
    const struct function_names names = entry_point_names(writer, script, entry_point);
    const bool area = returns_area(writer, entry_point);
    if (entry_point->optional && !area) {
        return;
    }
    text_format(writer->out, "\n/* The %sentry point %s. */\n",
                entry_point->optional ? "optional " : "", names.qualified);
    if (area) {
        write_return_definition(writer, &names);
    }
    if (entry_point->optional) {
        return;
    }
    text_append(writer->out, "\n");
    write_export_declaration(writer, script, &names);
    write_export_definition(writer, &names);
    if (returns_string(writer, entry_point)) {
        text_append(writer->out, "\n");
        write_post_declaration(writer, script, &names);
        write_post_definition(writer, &names);
    }
}

/* Writes the source of a kind of script's bindings, `<base>.c`. */
static void write_source(struct writer *writer, const struct script *script, const char *base,
                         const char *input) {
    write_banner(writer, input);
    text_format(writer->out,
                "\n#include \"%s.h\"\n\n#include <stddef.h>\n#include <stdint.h>\n"
                "#include <stdlib.h>\n#include <string.h>\n",
                base);
    write_notices(writer);
    write_runtime(writer, script);
    if (any_entry_point_returns_string(writer, script)) {
        write_copy_string(writer);
    }
    for (size_t i = 0; i < writer->interface->record_count; i++) {
        const struct record *record = writer->records[i];
        if (writer->contents.strings[record->index]) {
            write_free_definition(writer, record);
        }
    }
    for (size_t i = 0; i < writer->interface->group_count; i++) {
        const struct group *group = writer->groups[i];
        if (!uses(script, group)) {
            continue;
        }
        for (const struct item *item = group->items; item != NULL; item = item->next) {
            if (item->kind == ITEM_FUNCTION) {
                const struct function_names names =
                    function_names(writer, group, item->as.function);
                write_import_definition(writer, group, &names);
            } else if (item->kind == ITEM_OBJECT) {
                for (const struct function *method = item->as.object->methods; method != NULL;
                     method = method->next) {
                    const struct function_names names = method_names(writer, method);
                    write_import_definition(writer, group, &names);
                }
            }
        }
    }
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        write_entry_point_source(writer, script, entry_point);
    }
}

size_t c_bindings_file_count(const struct interface *interface) {
    size_t count = 0;
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        count += 2;
    }
    return count;
}

void generate_c_bindings(const struct interface *interface, const char *prefix, const char *input,
                         struct arena *arena, struct output_file *files) {
    struct writer writer;
    writer_init(&writer, interface, prefix, true, arena);
    size_t count = 0;
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        const char *base = arena_format(arena, "%s%s", prefix, script->name.text);

        struct text header;
        text_init(&header, arena);
        writer.out = &header;
        write_header(&writer, script, base, input);
        files[count].name = arena_format(arena, "%s.h", base);
        files[count].contents = header.data;
        files[count].size = header.length;
        count++;

        struct text source;
        text_init(&source, arena);
        writer.out = &source;
        write_source(&writer, script, base, input);
        files[count].name = arena_format(arena, "%s.c", base);
        files[count].contents = source.data;
        files[count].size = source.length;
        count++;
    }
}
