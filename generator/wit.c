#include "wit.h"

#include "arena.h"
#include "diagnostics.h"
#include "files.h"
#include "interface.h"
#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* WIT's keywords, as wasm-tools 1.262.0 reads them, each between spaces. `_` is left out, since no
 * name in an interface can be one. */
static const char keywords[] =
    " as async bool borrow char constructor enum error-context export f32 f64 flags from func"
    " future import include interface list map option own package record resource result s16 s32"
    " s64 s8 static stream string tuple type u16 u32 u64 u8 use variant with world ";

/* `name`, which is in snake_case, in kebab-case. */
static const char *kebab_case(const char *name, struct arena *arena) {
    char *kebab = arena_copy(arena, name, strlen(name));
    for (char *c = kebab; *c != '\0'; c++) {
        if (*c == '_') {
            *c = '-';
        }
    }
    return kebab;
}

/* How WIT spells `name`: in kebab-case, with a `%` before it if it's a keyword. */
static const char *wit_name(const char *name, struct arena *arena) {
    const char *kebab = kebab_case(name, arena);
    if (is_among(kebab, keywords)) {
        return arena_format(arena, "%%%s", kebab);
    }
    return kebab;
}

/* The name of an optional entry point's interface, `<kind>_<entry point>`, before WIT spells it. */
static const char *optional_interface_name(const struct script *script,
                                           const struct function *entry_point,
                                           struct arena *arena) {
    return arena_format(arena, "%s_%s", script->name.text, entry_point->name.text);
}

/* The record or object that `type` is, as a pointer that tells records and objects apart, or null
 * if it's neither. */
static const void *declaration_of(const struct type *type) {
    if (type->kind == TYPE_RECORD) {
        return type->record;
    }
    if (type->kind == TYPE_OBJECT) {
        return type->object;
    }
    return NULL;
}

/* The name of the record or object that `type` is. */
static const char *type_name(const struct type *type) {
    return type->kind == TYPE_RECORD ? type->record->name.text : type->object->name.text;
}

/* Whether WIT takes two names as the same. It spells them in kebab-case, and compares names without
 * their `-`s, so they're the same if they are once their `_`s are dropped. */
static bool same_to_wit(const char *a, const char *b) {
    for (;;) {
        while (*a == '_') {
            a++;
        }
        while (*b == '_') {
            b++;
        }
        if (*a != *b) {
            return false;
        }
        if (*a == '\0') {
            return true;
        }
        a++;
        b++;
    }
}

/* A name in one of the scopes that WIT tells names apart in, and what it names. */
struct scoped_name {
    /* The name, as the file spells it, or as the rendering makes it up. */
    const char *name;
    /* What it names, for a diagnostic, such as "record `world.vec2`". */
    const char *description;
    struct position position;
    /* Whether the name is declared in the scope, where the same name again is a duplicate, which
     * reading the interface reports, or among the records and objects that entry points take and
     * return, means the same type. */
    bool declared;
    /* The kind of script whose optional entry point's interface the name is, or null. */
    const struct script *optional_of;
};

/* The state of the check: the scope being checked. */
struct wit_check {
    struct arena *arena;
    struct diagnostics *diagnostics;
    struct scoped_name *names;
    size_t count;
    size_t capacity;
};

/* Adds a name to the scope being checked. */
static void add_scoped_name(struct wit_check *check, const char *name, const char *description,
                            struct position position, bool declared,
                            const struct script *optional_of) {
    check->names = arena_grow(check->arena, check->names, check->count, &check->capacity,
                              sizeof *check->names);
    struct scoped_name *item = &check->names[check->count++];
    item->name = name;
    item->description = description;
    item->position = position;
    item->declared = declared;
    item->optional_of = optional_of;
}

/* Adds a name declared in the scope being checked. */
static void add_declared_name(struct wit_check *check, const struct name *name,
                              const char *description) {
    add_scoped_name(check, name->text, description, name->position, true, NULL);
}

/* Whether two names of a scope clash in WIT, where nothing else reports them already. */
static bool clash(const struct scoped_name *a, const struct scoped_name *b) {
    if (!same_to_wit(a->name, b->name)) {
        return false;
    }
    if (a->declared && b->declared && strcmp(a->name, b->name) == 0) {
        return false;
    }
    if (a->optional_of != NULL && a->optional_of == b->optional_of) {
        /* Their entry points clash, which is reported in the kind of script's scope. */
        return false;
    }
    return true;
}

/* Reports each name of the scope being checked that clashes with one before it, and empties the
 * scope. */
static void check_scope(struct wit_check *check) {
    for (size_t i = 0; i < check->count; i++) {
        const struct scoped_name *name = &check->names[i];
        const struct scoped_name *earlier = NULL;
        for (size_t j = 0; j < check->count; j++) {
            const struct scoped_name *other = &check->names[j];
            if (compare_positions(other->position, name->position) >= 0 || !clash(name, other)) {
                continue;
            }
            if (earlier == NULL || compare_positions(other->position, earlier->position) < 0) {
                earlier = other;
            }
        }
        if (earlier == NULL) {
            continue;
        }
        const char *spelling = kebab_case(name->name, check->arena);
        const char *earlier_spelling = kebab_case(earlier->name, check->arena);
        if (strcmp(spelling, earlier_spelling) == 0) {
            report_error(check->diagnostics, name->position,
                         "%s and %s both come out in WIT as `%s`", name->description,
                         earlier->description, spelling);
        } else {
            report_error(check->diagnostics, name->position,
                         "%s and %s come out in WIT as `%s` and `%s`, which WIT takes as the same "
                         "name",
                         name->description, earlier->description, spelling, earlier_spelling);
        }
        report_note(check->diagnostics, earlier->position, "%s is here", earlier->description);
    }
    check->count = 0;
}

/* Checks a function's parameters, the function being described as `owner`. */
static void check_parameters(struct wit_check *check, const struct function *function,
                             const char *owner) {
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        add_declared_name(
            check, &parameter->name,
            arena_format(check->arena, "parameter `%s` of %s", parameter->name.text, owner));
    }
    check_scope(check);
}

/* Checks a record's fields. */
static void check_fields(struct wit_check *check, const struct record *record,
                         const char *qualified) {
    for (const struct field *field = record->fields; field != NULL; field = field->next) {
        add_declared_name(
            check, &field->name,
            arena_format(check->arena, "field `%s` of record `%s`", field->name.text, qualified));
    }
    check_scope(check);
}

/* Checks an object's methods, which WIT tells apart from each other and from the object, and each
 * method's parameters. */
static void check_methods(struct wit_check *check, const struct object *object,
                          const char *qualified) {
    struct arena *arena = check->arena;
    add_scoped_name(check, object->name.text,
                    arena_format(arena, "kind of host object `%s`", qualified),
                    object->name.position, false, NULL);
    for (const struct function *method = object->methods; method != NULL; method = method->next) {
        add_declared_name(check, &method->name,
                          arena_format(arena, "method `%s.%s`", qualified, method->name.text));
    }
    check_scope(check);
    for (const struct function *method = object->methods; method != NULL; method = method->next) {
        check_parameters(check, method,
                         arena_format(arena, "method `%s.%s`", qualified, method->name.text));
    }
}

/* Checks a group's declarations and the names its `use`s bring in, and what each declares. */
static void check_group(struct wit_check *check, const struct group *group) {
    struct arena *arena = check->arena;
    const char *scope = group->name.text;
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        switch (item->kind) {
        case ITEM_RECORD: {
            const struct name *name = &item->as.record->name;
            add_declared_name(check, name,
                              arena_format(arena, "record `%s.%s`", scope, name->text));
            break;
        }
        case ITEM_OBJECT: {
            const struct name *name = &item->as.object->name;
            add_declared_name(
                check, name, arena_format(arena, "kind of host object `%s.%s`", scope, name->text));
            break;
        }
        case ITEM_FUNCTION: {
            const struct name *name = &item->as.function->name;
            add_declared_name(check, name,
                              arena_format(arena, "host function `%s.%s`", scope, name->text));
            break;
        }
        case ITEM_USE:
            for (const struct use_item *used = item->as.use->items; used != NULL;
                 used = used->next) {
                add_declared_name(check, &used->name,
                                  arena_format(arena, "the `use` of `%s.%s`",
                                               item->as.use->group_name.text, used->name.text));
            }
            break;
        }
    }
    check_scope(check);

    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind == ITEM_RECORD) {
            check_fields(check, item->as.record,
                         arena_format(arena, "%s.%s", scope, item->as.record->name.text));
        } else if (item->kind == ITEM_OBJECT) {
            check_methods(check, item->as.object,
                          arena_format(arena, "%s.%s", scope, item->as.object->name.text));
        } else if (item->kind == ITEM_FUNCTION) {
            check_parameters(
                check, item->as.function,
                arena_format(arena, "host function `%s.%s`", scope, item->as.function->name.text));
        }
    }
}

/* Adds a record or object that an entry point names, at the place it's named, unless it's added
 * already. `named` holds what's added, as declaration_of gives them, and has room for each. */
static void add_named_type(struct wit_check *check, const struct type *type, const void **named) {
    const void *declaration = declaration_of(type);
    if (declaration == NULL) {
        return;
    }
    for (size_t i = 0; i < check->count; i++) {
        if (named[i] == declaration) {
            return;
        }
    }
    named[check->count] = declaration;
    const char *description = NULL;
    if (type->kind == TYPE_RECORD) {
        description = arena_format(check->arena, "record `%s.%s`", type->record->group->name.text,
                                   type->record->name.text);
    } else {
        description = arena_format(check->arena, "kind of host object `%s.%s`",
                                   type->object->group->name.text, type->object->name.text);
    }
    add_scoped_name(check, type_name(type), description, type->name.position, true, NULL);
}

/* Checks a kind of script's entry points, their parameters, and the records and objects they name,
 * which WIT uses by name in the kind's world or an optional entry point's interface. */
static void check_script(struct wit_check *check, const struct script *script) {
    struct arena *arena = check->arena;
    size_t types = 0;
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        add_declared_name(
            check, &entry_point->name,
            arena_format(arena, "entry point `%s.%s`", script->name.text, entry_point->name.text));
        for (const struct parameter *parameter = entry_point->parameters; parameter != NULL;
             parameter = parameter->next) {
            types++;
        }
        types++;
    }
    check_scope(check);

    const void **named = (const void **)arena_allocate(arena, (types + 1) * sizeof *named);
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        for (const struct parameter *parameter = entry_point->parameters; parameter != NULL;
             parameter = parameter->next) {
            add_named_type(check, &parameter->type, named);
        }
        if (entry_point->has_result) {
            add_named_type(check, &entry_point->result, named);
        }
    }
    check_scope(check);

    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        check_parameters(
            check, entry_point,
            arena_format(arena, "entry point `%s.%s`", script->name.text, entry_point->name.text));
    }
}

void check_wit_names(const struct interface *interface, struct arena *arena,
                     struct diagnostics *diagnostics) {
    struct wit_check check = {arena, diagnostics, NULL, 0, 0};
    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        add_declared_name(&check, &group->name,
                          arena_format(arena, "group `%s`", group->name.text));
    }
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        add_declared_name(&check, &script->name,
                          arena_format(arena, "kind of script `%s`", script->name.text));
        for (const struct function *entry_point = script->entry_points; entry_point != NULL;
             entry_point = entry_point->next) {
            if (entry_point->optional) {
                add_scoped_name(&check, optional_interface_name(script, entry_point, arena),
                                arena_format(arena, "optional entry point `%s.%s`",
                                             script->name.text, entry_point->name.text),
                                entry_point->name.position, false, script);
            }
        }
    }
    check_scope(&check);

    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        check_group(&check, group);
    }
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        check_script(&check, script);
    }
}

/* The state of writing the WIT rendering. */
struct writer {
    struct arena *arena;
    const struct interface *interface;
    /* The file being written. */
    struct text *out;
};

/* A type that an optional entry point's interface uses under a name of its own: the type's name in
 * the interface, and how WIT spells the name it's used as. */
struct alias {
    const char *name;
    const char *as;
};

/* Writes `head`, the items separated by ", ", and `tail`, on one line at `indent` if that fits.
 * Otherwise each item goes on a line of its own, 4 columns further in and followed by a comma, and
 * `tail` on a line of its own at `indent`. */
static void write_list(struct writer *writer, const char *indent, const char *head,
                       const struct strings *items, const char *tail) {
    if (strlen(indent) + strlen(head) + joined_length(items) + strlen(tail) <= LINE_LIMIT ||
        items->count == 0) {
        text_format(writer->out, "%s%s", indent, head);
        text_append_joined(writer->out, items);
        text_format(writer->out, "%s\n", tail);
        return;
    }
    text_format(writer->out, "%s%s\n", indent, head);
    for (size_t i = 0; i < items->count; i++) {
        text_format(writer->out, "%s    %s,\n", indent, items->items[i]);
    }
    text_format(writer->out, "%s%s\n", indent, tail);
}

/* Writes a doc comment from the interface at `indent`: each of its lines after a `///`, as the file
 * has it but for any spaces at its end. */
static void write_doc(struct writer *writer, const char *indent, const struct doc *doc) {
    for (size_t i = 0; i < doc->count; i++) {
        const char *line = doc->lines[i];
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == ' ' || line[length - 1] == '\t')) {
            length--;
        }
        text_format(writer->out, "%s///%.*s\n", indent, (int)length, line);
    }
}

/* How WIT spells each built-in type. */
static const char *const built_in_types[] = {
    [TYPE_U8] = "u8",   [TYPE_U16] = "u16", [TYPE_U32] = "u32",   [TYPE_U64] = "u64",
    [TYPE_S8] = "s8",   [TYPE_S16] = "s16", [TYPE_S32] = "s32",   [TYPE_S64] = "s64",
    [TYPE_F32] = "f32", [TYPE_F64] = "f64", [TYPE_BOOL] = "bool", [TYPE_STRING] = "string",
};

/* How WIT spells the name that `alias` gives the type called `name`, or null if it gives that type
 * none. */
static const char *alias_of(const struct alias *alias, const char *name) {
    if (alias == NULL || strcmp(alias->name, name) != 0) {
        return NULL;
    }
    return alias->as;
}

/* How WIT spells `type`, under the name that `alias` gives it, if any. A handle in a parameter, as
 * `parameter` says it is, is borrowed. */
static const char *wit_type(struct writer *writer, const struct type *type, bool parameter,
                            const struct alias *alias) {
    if (type->kind != TYPE_RECORD && type->kind != TYPE_OBJECT) {
        return built_in_types[type->kind];
    }
    const char *name = type_name(type);
    const char *spelt = alias_of(alias, name);
    if (spelt == NULL) {
        spelt = wit_name(name, writer->arena);
    }
    if (type->kind == TYPE_OBJECT && parameter) {
        return arena_format(writer->arena, "borrow<%s>", spelt);
    }
    return spelt;
}

/* Whether `function` has a parameter that WIT takes as called `name`. */
static bool has_parameter(const struct function *function, const char *name) {
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        if (same_to_wit(parameter->name.text, name)) {
            return true;
        }
    }
    return false;
}

/* The name that a method's parameter `name`, which WIT takes as `self`, takes beside the handle
 * that WIT calls `self`: `<name>_param`, or failing that, with `_param` after it as many more times
 * as it takes to name no other parameter. */
static const char *self_parameter_name(struct writer *writer, const struct function *method,
                                       const char *name) {
    const char *renamed = arena_format(writer->arena, "%s_param", name);
    while (has_parameter(method, renamed)) {
        renamed = arena_format(writer->arena, "%s_param", renamed);
    }
    return renamed;
}

/* Writes a host function, a method or an entry point at `indent`, after its doc comment, with
 * `head`, such as "export ", before its name, and with the name that `alias` gives a type, if
 * any. */
static void write_function(struct writer *writer, const char *indent, const char *head,
                           const struct function *function, const struct alias *alias) {
    struct arena *arena = writer->arena;
    write_doc(writer, indent, &function->doc);
    struct strings parameters = {NULL, 0, 0};
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        const char *name = parameter->name.text;
        if (function->object != NULL && same_to_wit(name, "self")) {
            name = self_parameter_name(writer, function, name);
        }
        strings_add(&parameters, writer->arena,
                    arena_format(arena, "%s: %s", wit_name(name, arena),
                                 wit_type(writer, &parameter->type, true, alias)));
    }
    const char *tail = ");";
    if (function->has_result) {
        tail = arena_format(arena, ") -> %s;", wit_type(writer, &function->result, false, alias));
    }
    write_list(writer, indent,
               arena_format(arena, "%s%s: func(", head, wit_name(function->name.text, arena)),
               &parameters, tail);
}

/* Writes a record. */
static void write_record(struct writer *writer, const struct record *record) {
    write_doc(writer, "    ", &record->doc);
    text_format(writer->out, "    record %s {\n", wit_name(record->name.text, writer->arena));
    for (const struct field *field = record->fields; field != NULL; field = field->next) {
        write_doc(writer, "        ", &field->doc);
        text_format(writer->out, "        %s: %s,\n", wit_name(field->name.text, writer->arena),
                    wit_type(writer, &field->type, false, NULL));
    }
    text_append(writer->out, "    }\n");
}

/* Writes a kind of host object, as a resource with its methods. */
static void write_resource(struct writer *writer, const struct object *object) {
    write_doc(writer, "    ", &object->doc);
    const char *name = wit_name(object->name.text, writer->arena);
    if (object->methods == NULL) {
        text_format(writer->out, "    resource %s;\n", name);
        return;
    }
    text_format(writer->out, "    resource %s {\n", name);
    for (const struct function *method = object->methods; method != NULL; method = method->next) {
        write_function(writer, "        ", "", method, NULL);
    }
    text_append(writer->out, "    }\n");
}

/* Writes a group's `use` of another group's records and objects. */
static void write_use(struct writer *writer, const struct use *use) {
    struct strings names = {NULL, 0, 0};
    for (const struct use_item *item = use->items; item != NULL; item = item->next) {
        strings_add(&names, writer->arena, wit_name(item->name.text, writer->arena));
    }
    write_list(
        writer, "    ",
        arena_format(writer->arena, "use %s.{", wit_name(use->group->name.text, writer->arena)),
        &names, "};");
}

/* Whether two declarations that follow each other in a group go on lines that follow each other, as
 * `use`s do, and host functions do. Others have a blank line between them. */
static bool kept_together(const struct item *previous, const struct item *item) {
    if (previous->kind != item->kind) {
        return false;
    }
    if (item->kind == ITEM_USE) {
        return true;
    }
    return item->kind == ITEM_FUNCTION;
}

/* Writes a group, as an interface. */
static void write_group(struct writer *writer, const struct group *group) {
    text_append(writer->out, "\n");
    write_doc(writer, "", &group->doc);
    const char *name = wit_name(group->name.text, writer->arena);
    if (group->items == NULL) {
        text_format(writer->out, "interface %s {}\n", name);
        return;
    }
    text_format(writer->out, "interface %s {\n", name);
    const struct item *previous = NULL;
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (previous != NULL && !kept_together(previous, item)) {
            text_append(writer->out, "\n");
        }
        previous = item;
        switch (item->kind) {
        case ITEM_RECORD:
            write_record(writer, item->as.record);
            break;
        case ITEM_OBJECT:
            write_resource(writer, item->as.object);
            break;
        case ITEM_FUNCTION:
            write_function(writer, "    ", "", item->as.function, NULL);
            break;
        case ITEM_USE:
            write_use(writer, item->as.use);
            break;
        }
    }
    text_append(writer->out, "}\n");
}

/* The record or object that a group's declaration declares, as declaration_of gives it, or null if
 * it declares neither. */
static const void *item_declaration(const struct item *item) {
    if (item->kind == ITEM_RECORD) {
        return item->as.record;
    }
    if (item->kind == ITEM_OBJECT) {
        return item->as.object;
    }
    return NULL;
}

/* Whether `function` takes or returns the record or object `declaration`, as declaration_of gives
 * it. */
static bool takes_or_returns(const struct function *function, const void *declaration) {
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        if (declaration_of(&parameter->type) == declaration) {
            return true;
        }
    }
    if (!function->has_result) {
        return false;
    }
    return declaration_of(&function->result) == declaration;
}

/* Whether `only` takes or returns the record or object `declaration`, or if `only` is null, whether
 * any of `script`'s required entry points does. */
static bool entry_points_take_or_return(const struct script *script, const struct function *only,
                                        const void *declaration) {
    if (only != NULL) {
        return takes_or_returns(only, declaration);
    }
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        if (!entry_point->optional && takes_or_returns(entry_point, declaration)) {
            return true;
        }
    }
    return false;
}

/* Writes a `use` of each group's records and objects that the entry points `only` picks take or
 * return, as entry_points_take_or_return picks them, with the name that `alias` gives a type, if
 * any. Returns whether it wrote any. */
static bool write_type_uses(struct writer *writer, const struct script *script,
                            const struct function *only, const struct alias *alias) {
    struct arena *arena = writer->arena;
    bool any = false;
    for (const struct group *group = writer->interface->groups; group != NULL;
         group = group->next) {
        struct strings names = {NULL, 0, 0};
        for (const struct item *item = group->items; item != NULL; item = item->next) {
            const void *declaration = item_declaration(item);
            if (declaration == NULL || !entry_points_take_or_return(script, only, declaration)) {
                continue;
            }
            const char *name =
                item->kind == ITEM_RECORD ? item->as.record->name.text : item->as.object->name.text;
            const char *as = alias_of(alias, name);
            if (as != NULL) {
                strings_add(&names, writer->arena,
                            arena_format(arena, "%s as %s", wit_name(name, arena), as));
            } else {
                strings_add(&names, writer->arena, wit_name(name, arena));
            }
        }
        if (names.count > 0) {
            write_list(writer, "    ",
                       arena_format(arena, "use %s.{", wit_name(group->name.text, arena)), &names,
                       "};");
            any = true;
        }
    }
    return any;
}

/* Writes a kind of script, as a world that imports the groups it uses and exports its required
 * entry points. */
static void write_world(struct writer *writer, const struct script *script) {
    text_append(writer->out, "\n");
    write_doc(writer, "", &script->doc);
    const char *name = wit_name(script->name.text, writer->arena);
    bool any_required = false;
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        if (!entry_point->optional) {
            any_required = true;
        }
    }
    if (script->uses == NULL && !any_required) {
        text_format(writer->out, "world %s {}\n", name);
        return;
    }
    text_format(writer->out, "world %s {\n", name);
    for (const struct script_use *use = script->uses; use != NULL; use = use->next) {
        text_format(writer->out, "    import %s;\n",
                    wit_name(use->group->name.text, writer->arena));
    }
    const bool uses_types = write_type_uses(writer, script, NULL, NULL);
    if (any_required && (script->uses != NULL || uses_types)) {
        text_append(writer->out, "\n");
    }
    for (const struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        if (!entry_point->optional) {
            write_function(writer, "    ", "export ", entry_point, NULL);
        }
    }
    text_append(writer->out, "}\n");
}

/* The name of a record or an object that `function` names, as a parameter's type or its result,
 * and that WIT takes as called `name`, or null if it names none. */
static const char *type_called(const struct function *function, const char *name) {
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        if (declaration_of(&parameter->type) != NULL &&
            same_to_wit(type_name(&parameter->type), name)) {
            return type_name(&parameter->type);
        }
    }
    if (function->has_result && declaration_of(&function->result) != NULL &&
        same_to_wit(type_name(&function->result), name)) {
        return type_name(&function->result);
    }
    return NULL;
}

/* Sets `alias` to the name under which an optional entry point's interface uses the type that WIT
 * takes as having the entry point's name, and returns it, or returns null if the entry point names
 * no such type. The type is used as `<name>_type`, or failing that, with `_type` after it as many
 * more times as it takes to name no other type the entry point names. */
static const struct alias *
entry_point_alias(struct writer *writer, const struct function *entry_point, struct alias *alias) {
    const char *name = type_called(entry_point, entry_point->name.text);
    if (name == NULL) {
        return NULL;
    }
    const char *as = arena_format(writer->arena, "%s_type", name);
    while (type_called(entry_point, as) != NULL) {
        as = arena_format(writer->arena, "%s_type", as);
    }
    alias->name = name;
    alias->as = wit_name(as, writer->arena);
    return alias;
}

/* Writes an optional entry point's interface, which a script's world exports if the script provides
 * the entry point. */
static void write_optional_interface(struct writer *writer, const struct script *script,
                                     const struct function *entry_point) {
    struct arena *arena = writer->arena;
    text_append(writer->out, "\n");
    text_fill(writer->out, "///",
              arena_format(arena,
                           "The optional entry point `%s.%s`, which a script's world exports if "
                           "the script provides it.",
                           script->name.text, entry_point->name.text));
    text_format(writer->out, "interface %s {\n",
                wit_name(optional_interface_name(script, entry_point, arena), arena));
    struct alias storage;
    const struct alias *alias = entry_point_alias(writer, entry_point, &storage);
    if (write_type_uses(writer, script, entry_point, alias)) {
        text_append(writer->out, "\n");
    }
    write_function(writer, "    ", "", entry_point, alias);
    text_append(writer->out, "}\n");
}

void generate_wit(const struct interface *interface, const char *input, struct arena *arena,
                  struct output_file *file) {
    struct text out;
    text_init(&out, arena);
    struct writer writer = {arena, interface, &out};
    text_format(&out, "/* Generated by stasis-gen %s from %s. Do not edit. */\n\n", STASIS_VERSION,
                input);
    write_doc(&writer, "", &interface->doc);
    text_format(&out, "package %s:%s@%lu.%lu.%lu;\n", wit_name(interface->space.text, arena),
                wit_name(interface->name.text, arena), (unsigned long)interface->version.major,
                (unsigned long)interface->version.minor, (unsigned long)interface->version.patch);
    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        write_group(&writer, group);
    }
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        write_world(&writer, script);
        for (const struct function *entry_point = script->entry_points; entry_point != NULL;
             entry_point = entry_point->next) {
            if (entry_point->optional) {
                write_optional_interface(&writer, script, entry_point);
            }
        }
    }
    file->name = arena_format(arena, "%s-%s.wit", kebab_case(interface->space.text, arena),
                              kebab_case(interface->name.text, arena));
    file->contents = out.data;
    file->size = out.length;
}
