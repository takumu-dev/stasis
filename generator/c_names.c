#include "c_names.h"

#include "arena.h"
#include "diagnostics.h"
#include "interface.h"
#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* The keywords of C up to C23 and of C++ up to C++26 that a name in an interface can spell, the
 * alternative tokens included, each between spaces. */
static const char keywords[] =
    " alignas alignof and and_eq asm auto bitand bitor bool break case catch char char16_t"
    " char32_t char8_t class co_await co_return co_yield compl concept const const_cast consteval"
    " constexpr constinit continue contract_assert decltype default delete do double dynamic_cast"
    " else enum explicit export extern false float for friend goto if inline int long mutable"
    " namespace new noexcept not not_eq nullptr operator or or_eq private protected public"
    " register reinterpret_cast requires restrict return short signed sizeof static static_assert"
    " static_cast struct switch template this thread_local throw true try typedef typeid typename"
    " typeof typeof_unqual union unsigned using virtual void volatile wchar_t while xor xor_eq ";

/* The lowercase object-like macros that common headers or compilers define, each between
 * spaces. */
static const char macros[] =
    " bool errno false far interface linux near small stderr stdin stdout true unix ";

/* The generator's own parameters, beside which a parameter of an interface's is declared, each
 * between spaces. */
static const char own_parameters[] = " instance result self ";

/* Whether `c` is a lowercase ASCII letter. */
static bool is_lowercase(char c) {
    if (c < 'a') {
        return false;
    }
    return c <= 'z';
}

const char *prefix_problem(const char *prefix) {
    const size_t length = strlen(prefix);
    if (strncmp(prefix, "stasis_", 7) == 0) {
        return "starts with `stasis_`, which is reserved for Stasis's own names";
    }
    const char *problem = "isn't lowercase snake_case with a `_` after its last word";
    if (length == 0 || prefix[length - 1] != '_') {
        return problem;
    }
    bool word_start = true;
    for (size_t i = 0; i < length; i++) {
        const char c = prefix[i];
        if (word_start) {
            if (!is_lowercase(c)) {
                return problem;
            }
            word_start = false;
        } else if (c == '_') {
            word_start = true;
        } else if (!is_lowercase(c) && (c < '0' || c > '9')) {
            return problem;
        }
    }
    return NULL;
}

const char *default_prefix(const struct interface *interface, struct arena *arena) {
    if (interface->space.text == NULL || interface->name.text == NULL) {
        return "";
    }
    return arena_format(arena, "%s_%s_", interface->space.text, interface->name.text);
}

const char *c_member_name(const char *name, struct arena *arena) {
    if (is_among(name, keywords) || is_among(name, macros) || is_among(name, own_parameters)) {
        return arena_format(arena, "%s_", name);
    }
    return name;
}

const char *c_record_name(const struct record *record, struct arena *arena) {
    return arena_format(arena, "%s_%s", record->group->name.text, record->name.text);
}

const char *c_object_name(const struct object *object, struct arena *arena) {
    return arena_format(arena, "%s_%s", object->group->name.text, object->name.text);
}

const char *c_function_name(const struct group *group, const struct function *function,
                            struct arena *arena) {
    return arena_format(arena, "%s_%s", group->name.text, function->name.text);
}

const char *c_method_name(const struct function *method, struct arena *arena) {
    return arena_format(arena, "%s_%s", c_object_name(method->object, arena), method->name.text);
}

const char *c_entry_point_name(const struct script *script, const struct function *entry_point,
                               struct arena *arena) {
    return arena_format(arena, "%s_%s", script->name.text, entry_point->name.text);
}

const char *c_uppercase(const char *name, struct arena *arena) {
    char *uppercase = arena_copy(arena, name, strlen(name));
    for (char *c = uppercase; *c != '\0'; c++) {
        if (is_lowercase(*c)) {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return uppercase;
}

/* The names besides its own that the host's side spells where a parameter or field is in scope,
 * which a parameter or field can't take: the types it names, and the call its typed callers make,
 * each between spaces. */
static const char used_names[] =
    " int8_t int16_t int32_t int64_t stasis_call stasis_instance stasis_status stasis_string"
    " uint8_t uint16_t uint32_t uint64_t ";

/* A name that the host's side or the C bindings declare, after the prefix, and the declaration it
 * comes from. */
struct c_name {
    const char *name;
    /* What the declaration is, for a diagnostic, such as "host function `world.find`". */
    const char *description;
    /* The declaration's name in the interface, with its scopes, such as "world.find". Two
     * declarations with the same one are a duplicate, which the checker reports. */
    const char *qualified;
    struct position position;
};

/* A parameter or a field, whose C name isn't prefixed. */
struct member {
    const struct name *name;
    /* What it is, for a diagnostic, such as "parameter `name` of host function `world.find`". */
    const char *description;
};

/* The state of the check. */
struct clash_check {
    struct arena *arena;
    struct diagnostics *diagnostics;
    const char *prefix;
    /* The names that declarations come out as. */
    struct c_name *names;
    size_t name_count;
    size_t name_capacity;
    /* Every parameter and field. */
    struct member *members;
    size_t member_count;
    size_t member_capacity;
    /* Whether each record holds a string, by the record's index. */
    bool *holds_string;
};

/* Adds a name that a declaration comes out as, with what the declaration is. */
static void add_c_name(struct clash_check *check, const char *name, const char *description,
                       const char *qualified, struct position position) {
    check->names = arena_grow(check->arena, check->names, check->name_count, &check->name_capacity,
                              sizeof *check->names);
    struct c_name *item = &check->names[check->name_count++];
    item->name = name;
    item->description = description;
    item->qualified = qualified;
    item->position = position;
}

/* Adds a name that a declaration of the interface's, called `qualified`, comes out as. `what` says
 * what the declaration is, such as "host function". */
static void add_declaration(struct clash_check *check, const char *name, const char *what,
                            const char *qualified, struct position position) {
    add_c_name(check, name, arena_format(check->arena, "%s `%s`", what, qualified), qualified,
               position);
}

/* Adds a parameter or field, `what` saying which it is, of the declaration described as
 * `owner`. */
static void add_member(struct clash_check *check, const struct name *name, const char *what,
                       const char *owner) {
    check->members = arena_grow(check->arena, check->members, check->member_count,
                                &check->member_capacity, sizeof *check->members);
    struct member *member = &check->members[check->member_count++];
    member->name = name;
    member->description = arena_format(check->arena, "%s `%s` of %s", what, name->text, owner);
}

/* Adds a function's parameters, the function being described as `owner`. */
static void add_parameters(struct clash_check *check, const struct function *function,
                           const char *owner) {
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        add_member(check, &parameter->name, "parameter", owner);
    }
}

/* Works out which records hold a string, in a field of their own or of a record they hold, which
 * gives them a `_free` helper in the C bindings. */
static void find_records_holding_strings(struct clash_check *check,
                                         const struct interface *interface) {
    check->holds_string =
        arena_allocate(check->arena, (interface->record_count + 1) * sizeof *check->holds_string);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const struct group *group = interface->groups; group != NULL; group = group->next) {
            for (const struct item *item = group->items; item != NULL; item = item->next) {
                if (item->kind != ITEM_RECORD || check->holds_string[item->as.record->index]) {
                    continue;
                }
                for (const struct field *field = item->as.record->fields; field != NULL;
                     field = field->next) {
                    const struct type *type = &field->type;
                    if (type->kind == TYPE_STRING ||
                        (type->kind == TYPE_RECORD && check->holds_string[type->record->index])) {
                        check->holds_string[item->as.record->index] = true;
                        changed = true;
                    }
                }
            }
        }
    }
}

/* Adds a record's name, its `_free` helper's if it holds a string, and its fields. */
static void add_record(struct clash_check *check, const struct record *record) {
    const char *qualified =
        arena_format(check->arena, "%s.%s", record->group->name.text, record->name.text);
    const char *name = c_record_name(record, check->arena);
    add_declaration(check, name, "record", qualified, record->name.position);
    if (check->holds_string[record->index]) {
        add_c_name(check, arena_format(check->arena, "%s_free", name),
                   arena_format(check->arena, "the `_free` helper of record `%s`", qualified),
                   arena_format(check->arena, "%s:free", qualified), record->name.position);
    }
    const char *owner = arena_format(check->arena, "record `%s`", qualified);
    for (const struct field *field = record->fields; field != NULL; field = field->next) {
        add_member(check, &field->name, "field", owner);
    }
}

/* Adds the names of a group's declarations, and their parameters and fields. */
static void add_group(struct clash_check *check, const struct group *group) {
    struct arena *arena = check->arena;
    const char *scope = group->name.text;
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        switch (item->kind) {
        case ITEM_RECORD:
            add_record(check, item->as.record);
            break;
        case ITEM_OBJECT: {
            const struct object *object = item->as.object;
            const char *qualified = arena_format(arena, "%s.%s", scope, object->name.text);
            add_declaration(check, c_object_name(object, arena), "kind of host object", qualified,
                            object->name.position);
            for (const struct function *method = object->methods; method != NULL;
                 method = method->next) {
                const char *method_qualified =
                    arena_format(arena, "%s.%s", qualified, method->name.text);
                add_declaration(check, c_method_name(method, arena), "method", method_qualified,
                                method->name.position);
                add_parameters(check, method, arena_format(arena, "method `%s`", method_qualified));
            }
            break;
        }
        case ITEM_FUNCTION: {
            const struct function *function = item->as.function;
            const char *qualified = arena_format(arena, "%s.%s", scope, function->name.text);
            add_declaration(check, c_function_name(group, function, arena), "host function",
                            qualified, function->name.position);
            add_parameters(check, function, arena_format(arena, "host function `%s`", qualified));
            break;
        }
        case ITEM_USE:
            break;
        }
    }
}

/* Orders names alphabetically, and the same names by where their declarations are. */
static int compare_c_names(const void *a, const void *b) {
    const struct c_name *first = a;
    const struct c_name *second = b;
    const int order = strcmp(first->name, second->name);
    if (order != 0) {
        return order;
    }
    return compare_positions(first->position, second->position);
}

/* Orders a name being looked for against a name. */
static int compare_to_c_name(const void *key, const void *item) {
    return strcmp(key, ((const struct c_name *)item)->name);
}

/* Reports each declaration whose name is that of one declared before it. */
static void report_clashing_declarations(struct clash_check *check) {
    size_t first = 0;
    for (size_t i = 1; i < check->name_count; i++) {
        const struct c_name *name = &check->names[i];
        if (strcmp(name->name, check->names[first].name) != 0) {
            first = i;
            continue;
        }
        const struct c_name *earlier = &check->names[first];
        if (strcmp(name->qualified, earlier->qualified) == 0) {
            continue;
        }
        report_error(check->diagnostics, name->position, "%s and %s both come out in C as `%s%s`",
                     name->description, earlier->description, check->prefix, name->name);
        report_note(check->diagnostics, earlier->position, "%s is here", earlier->description);
    }
}

/* Reports a parameter or field whose C name is one the host's side spells where it's in scope:
 * a type or call of Stasis's or the C library's, or a declaration's prefixed name. */
static void check_member(struct clash_check *check, const struct member *member) {
    const char *name = c_member_name(member->name->text, check->arena);
    if (is_among(name, used_names)) {
        report_error(check->diagnostics, member->name->position,
                     "%s comes out in C as `%s`, a name the host's side uses already",
                     member->description, name);
        return;
    }
    const size_t length = strlen(check->prefix);
    if (length == 0 || strncmp(name, check->prefix, length) != 0) {
        return;
    }
    const struct c_name *found = bsearch(name + length, check->names, check->name_count,
                                         sizeof *check->names, compare_to_c_name);
    if (found != NULL) {
        report_error(check->diagnostics, member->name->position,
                     "%s and %s both come out in C as `%s`", member->description,
                     found->description, name);
        report_note(check->diagnostics, found->position, "%s is here", found->description);
    }
}

void check_c_names(const struct interface *interface, const char *prefix, struct arena *arena,
                   struct diagnostics *diagnostics) {
    struct clash_check check;
    memset(&check, 0, sizeof check);
    check.arena = arena;
    check.diagnostics = diagnostics;
    check.prefix = prefix;
    find_records_holding_strings(&check, interface);
    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        add_group(&check, group);
    }
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        for (const struct function *entry_point = script->entry_points; entry_point != NULL;
             entry_point = entry_point->next) {
            const char *qualified =
                arena_format(arena, "%s.%s", script->name.text, entry_point->name.text);
            add_declaration(&check, c_entry_point_name(script, entry_point, arena), "entry point",
                            qualified, entry_point->name.position);
            add_parameters(&check, entry_point, arena_format(arena, "entry point `%s`", qualified));
        }
    }
    add_c_name(&check, "interface", "the interface's descriptors", "", interface->position);

    qsort(check.names, check.name_count, sizeof *check.names, compare_c_names);
    report_clashing_declarations(&check);
    for (size_t i = 0; i < check.member_count; i++) {
        check_member(&check, &check.members[i]);
    }
}
