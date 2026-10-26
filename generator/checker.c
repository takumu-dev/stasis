#include "checker.h"

#include "arena.h"
#include "diagnostics.h"
#include "interface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* What a name in a group's scope stands for. */
enum entry_kind {
    ENTRY_RECORD,
    ENTRY_OBJECT,
    ENTRY_FUNCTION,
    /* A name that a `use` failed to bring in, which has been reported. */
    ENTRY_UNRESOLVED,
};

/* A name in a group's scope: one the group declares, or one a `use` brings in. */
struct entry {
    const struct name *name;
    enum entry_kind kind;
    const struct record *record;
    const struct object *object;
};

/* The names in a group's scope. */
struct scope {
    struct entry *entries;
    size_t count;
    size_t capacity;
};

/* A list of names, to check that none comes twice. */
struct names {
    const struct name **items;
    size_t count;
    size_t capacity;
};

/* The state of the checker. */
struct checker {
    struct interface *interface;
    struct arena *arena;
    struct diagnostics *diagnostics;
    /* Each group's scope, by the group's index. */
    struct scope *scopes;
};

/* Adds a name to a list of names. */
static void add_name(struct checker *checker, struct names *names, const struct name *name) {
    names->items = (const struct name **)arena_grow(
        checker->arena, (void *)names->items, names->count, &names->capacity, sizeof *names->items);
    names->items[names->count++] = name;
}

/* Orders names by where they are. */
static int compare_names(const void *a, const void *b) {
    return compare_positions((*(const struct name *const *)a)->position,
                             (*(const struct name *const *)b)->position);
}

/* Reports each name that comes again after it first comes, as a duplicate in `scope`. */
static void check_unique(struct checker *checker, struct names *names, const char *scope) {
    if (names->count == 0) {
        return;
    }
    qsort((void *)names->items, names->count, sizeof *names->items, compare_names);
    for (size_t i = 1; i < names->count; i++) {
        const struct name *name = names->items[i];
        for (size_t j = 0; j < i; j++) {
            if (strcmp(name->text, names->items[j]->text) == 0) {
                report_error(checker->diagnostics, name->position, "duplicate name `%s` in %s",
                             name->text, scope);
                report_note(checker->diagnostics, names->items[j]->position,
                            "the first `%s` is here", name->text);
                break;
            }
        }
    }
}

/* The group called `name`, or null. */
static struct group *find_group(const struct interface *interface, const char *name) {
    for (struct group *group = interface->groups; group != NULL; group = group->next) {
        if (strcmp(group->name.text, name) == 0) {
            return group;
        }
    }
    return NULL;
}

/* Whether a kind of script is called `name`. */
static bool is_script(const struct interface *interface, const char *name) {
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        if (strcmp(script->name.text, name) == 0) {
            return true;
        }
    }
    return false;
}

/* Reports a reference to a group that doesn't exist. */
static void report_no_group(struct checker *checker, const struct name *name) {
    if (is_script(checker->interface, name->text)) {
        report_error(checker->diagnostics, name->position, "`%s` is a kind of script, not a group",
                     name->text);
    } else {
        report_error(checker->diagnostics, name->position, "there's no group `%s`", name->text);
    }
}

/* The name of an item that a group declares, or null for a `use`. */
static const struct name *declared_name(const struct item *item) {
    switch (item->kind) {
    case ITEM_RECORD:
        return &item->as.record->name;
    case ITEM_OBJECT:
        return &item->as.object->name;
    case ITEM_FUNCTION:
        return &item->as.function->name;
    case ITEM_USE:
        break;
    }
    return NULL;
}

/* The item called `name` that `group` declares itself, or null. */
static const struct item *find_declaration(const struct group *group, const char *name) {
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        const struct name *declared = declared_name(item);
        if (declared != NULL && strcmp(declared->text, name) == 0) {
            return item;
        }
    }
    return NULL;
}

/* Adds `name` to a group's scope, standing for what `kind` says: `record` for ENTRY_RECORD, and
 * `object` for ENTRY_OBJECT. */
static void add_entry(struct checker *checker, const struct group *group, const struct name *name,
                      enum entry_kind kind, const struct record *record,
                      const struct object *object) {
    struct scope *scope = &checker->scopes[group->index];
    scope->entries = arena_grow(checker->arena, scope->entries, scope->count, &scope->capacity,
                                sizeof *scope->entries);
    struct entry *entry = &scope->entries[scope->count++];
    entry->name = name;
    entry->kind = kind;
    entry->record = record;
    entry->object = object;
}

/* The entry for `name` in a group's scope, or null. */
static const struct entry *look_up(const struct checker *checker, const struct group *group,
                                   const char *name) {
    const struct scope *scope = &checker->scopes[group->index];
    for (size_t i = 0; i < scope->count; i++) {
        if (strcmp(scope->entries[i].name->text, name) == 0) {
            return &scope->entries[i];
        }
    }
    return NULL;
}

/* Puts what a group declares in its scope. */
static void add_declarations(struct checker *checker, const struct group *group) {
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        switch (item->kind) {
        case ITEM_RECORD:
            add_entry(checker, group, &item->as.record->name, ENTRY_RECORD, item->as.record, NULL);
            break;
        case ITEM_OBJECT:
            add_entry(checker, group, &item->as.object->name, ENTRY_OBJECT, NULL, item->as.object);
            break;
        case ITEM_FUNCTION:
            add_entry(checker, group, &item->as.function->name, ENTRY_FUNCTION, NULL, NULL);
            break;
        case ITEM_USE:
            break;
        }
    }
}

/* The item of one of `group`'s own `use`s that's called `name`, or null. */
static const struct use_item *find_used(const struct group *group, const char *name,
                                        const struct use **found) {
    for (const struct item *item = group->items; item != NULL; item = item->next) {
        if (item->kind != ITEM_USE) {
            continue;
        }
        for (const struct use_item *used = item->as.use->items; used != NULL; used = used->next) {
            if (strcmp(used->name.text, name) == 0) {
                *found = item->as.use;
                return used;
            }
        }
    }
    return NULL;
}

/* Resolves one name that a `use` brings in from `from`, and puts it in `group`'s scope. */
static void resolve_use_item(struct checker *checker, const struct group *group,
                             const struct group *from, struct use_item *item) {
    enum entry_kind kind = ENTRY_UNRESOLVED;
    const struct item *declaration = find_declaration(from, item->name.text);
    const struct use *use = NULL;
    const struct use_item *used = NULL;
    if (declaration == NULL) {
        used = find_used(from, item->name.text, &use);
    }
    if (declaration != NULL && declaration->kind == ITEM_RECORD) {
        kind = ENTRY_RECORD;
        item->record = declaration->as.record;
    } else if (declaration != NULL && declaration->kind == ITEM_OBJECT) {
        kind = ENTRY_OBJECT;
        item->object = declaration->as.object;
    } else if (declaration != NULL) {
        report_error(checker->diagnostics, item->name.position,
                     "`%s` is a host function, and `use` brings in only records and objects",
                     item->name.text);
    } else if (used != NULL) {
        report_error(checker->diagnostics, item->name.position,
                     "group `%s` doesn't declare `%s`, and `use` brings in only what a group "
                     "declares itself",
                     from->name.text, item->name.text);
        report_note(checker->diagnostics, used->name.position,
                    "`%s` brings it in from group `%s` here", from->name.text,
                    use->group_name.text);
    } else {
        report_error(checker->diagnostics, item->name.position,
                     "group `%s` declares no record or object `%s`", from->name.text,
                     item->name.text);
    }
    add_entry(checker, group, &item->name, kind, item->record, item->object);
}

/* Resolves the group a `use` names, and what it brings in from there. */
static void resolve_use(struct checker *checker, const struct group *group, struct use *use) {
    const struct group *from = find_group(checker->interface, use->group_name.text);
    if (from == group) {
        /* What it would bring in is in the group's scope already. */
        report_error(checker->diagnostics, use->group_name.position, "group `%s` can't use itself",
                     group->name.text);
        return;
    }
    if (from == NULL) {
        report_no_group(checker, &use->group_name);
    }
    for (struct use_item *item = use->items; item != NULL; item = item->next) {
        if (from == NULL) {
            add_entry(checker, group, &item->name, ENTRY_UNRESOLVED, NULL, NULL);
        } else {
            resolve_use_item(checker, group, from, item);
        }
    }
    use->group = from;
}

/* Reports a type that isn't known where it's used, and where something of its name is declared. */
static void report_unknown_type(struct checker *checker, struct type *type) {
    report_error(checker->diagnostics, type->name.position, "unknown type `%s`", type->name.text);
    for (const struct group *group = checker->interface->groups; group != NULL;
         group = group->next) {
        const struct item *item = find_declaration(group, type->name.text);
        if (item != NULL && item->kind != ITEM_FUNCTION) {
            report_note(checker->diagnostics, declared_name(item)->position,
                        "`%s` is declared in group `%s`", type->name.text, group->name.text);
            break;
        }
    }
    type->kind = TYPE_INVALID;
}

/* Resolves a type to what `entry` stands for. */
static void resolve_to(struct checker *checker, struct type *type, const struct entry *entry) {
    switch (entry->kind) {
    case ENTRY_RECORD:
        type->kind = TYPE_RECORD;
        type->record = entry->record;
        return;
    case ENTRY_OBJECT:
        type->kind = TYPE_OBJECT;
        type->object = entry->object;
        return;
    case ENTRY_FUNCTION:
        report_error(checker->diagnostics, type->name.position,
                     "`%s` is a host function, not a type", type->name.text);
        break;
    case ENTRY_UNRESOLVED:
        break;
    }
    type->kind = TYPE_INVALID;
}

/* Resolves a type that a group's declaration names, in the group's scope. */
static void resolve_type(struct checker *checker, const struct group *group, struct type *type) {
    if (type->kind != TYPE_NAMED) {
        return;
    }
    const struct entry *entry = look_up(checker, group, type->name.text);
    if (entry == NULL) {
        report_unknown_type(checker, type);
    } else {
        resolve_to(checker, type, entry);
    }
}

/* Whether two entries stand for the same record or object. */
static bool same_type(const struct entry *a, const struct entry *b) {
    if (a->record != b->record) {
        return false;
    }
    return a->object == b->object;
}

/* The entry for an entry point's type in the scope of one of the groups its kind of script uses,
 * if it stands for a record or an object there. */
static const struct entry *look_up_type(const struct checker *checker, const struct script_use *use,
                                        const char *name) {
    if (use->group == NULL) {
        return NULL;
    }
    const struct entry *entry = look_up(checker, use->group, name);
    if (entry == NULL || entry->kind == ENTRY_FUNCTION) {
        return NULL;
    }
    return entry;
}

/* Reports a type that an entry point names, which the groups its kind of script uses give
 * different meanings, with a note at each meaning's declaration. */
static void report_ambiguous_type(struct checker *checker, const struct script *script,
                                  const struct type *type) {
    report_error(checker->diagnostics, type->name.position,
                 "`%s` is ambiguous: the groups that `%s` uses have more than one", type->name.text,
                 script->name.text);
    /* Each meaning is noted once, for the first group that has it. */
    for (const struct script_use *use = script->uses; use != NULL; use = use->next) {
        const struct entry *entry = look_up_type(checker, use, type->name.text);
        for (const struct script_use *earlier = script->uses; entry != NULL && earlier != use;
             earlier = earlier->next) {
            const struct entry *other = look_up_type(checker, earlier, type->name.text);
            if (other != NULL && same_type(entry, other)) {
                entry = NULL;
            }
        }
        if (entry != NULL) {
            const struct name *name =
                entry->record != NULL ? &entry->record->name : &entry->object->name;
            const struct group *group =
                entry->record != NULL ? entry->record->group : entry->object->group;
            report_note(checker->diagnostics, name->position, "group `%s` declares one here",
                        group->name.text);
        }
    }
}

/* Resolves a type that an entry point names, in the scopes of the groups its kind of script uses.
 * A name that two of those groups give different meanings is ambiguous. */
static void resolve_in_script(struct checker *checker, const struct script *script,
                              struct type *type) {
    if (type->kind != TYPE_NAMED) {
        return;
    }
    const struct entry *found = NULL;
    const struct entry *function = NULL;
    bool ambiguous = false;
    for (const struct script_use *use = script->uses; use != NULL; use = use->next) {
        const struct entry *entry =
            use->group == NULL ? NULL : look_up(checker, use->group, type->name.text);
        if (entry == NULL) {
            continue;
        }
        if (entry->kind == ENTRY_UNRESOLVED) {
            type->kind = TYPE_INVALID;
            return;
        }
        if (entry->kind == ENTRY_FUNCTION) {
            function = entry;
        } else if (found == NULL) {
            found = entry;
        } else if (!same_type(entry, found)) {
            ambiguous = true;
        }
    }
    if (ambiguous) {
        report_ambiguous_type(checker, script, type);
        type->kind = TYPE_INVALID;
    } else if (found != NULL) {
        resolve_to(checker, type, found);
    } else if (function != NULL) {
        resolve_to(checker, type, function);
    } else {
        report_unknown_type(checker, type);
    }
}

/* Resolves a type: in `group`'s scope for a host function or a method, and through `script`'s
 * `uses` for an entry point. */
static void resolve(struct checker *checker, const struct group *group, const struct script *script,
                    struct type *type) {
    if (script != NULL) {
        resolve_in_script(checker, script, type);
    } else {
        resolve_type(checker, group, type);
    }
}

/* Checks a function's parameters, and resolves their types and its result's, as resolve does. */
static void check_function(struct checker *checker, struct function *function,
                           const struct group *group, const struct script *script) {
    struct names names = {0};
    for (struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        add_name(checker, &names, &parameter->name);
        resolve(checker, group, script, &parameter->type);
    }
    if (function->has_result) {
        resolve(checker, group, script, &function->result);
    }
    check_unique(checker, &names,
                 arena_format(checker->arena, "the parameters of `%s`", function->name.text));
}

/* Checks a record's fields and resolves their types. */
static void check_record(struct checker *checker, struct record *record) {
    struct names names = {0};
    for (struct field *field = record->fields; field != NULL; field = field->next) {
        add_name(checker, &names, &field->name);
        resolve_type(checker, record->group, &field->type);
    }
    check_unique(checker, &names, arena_format(checker->arena, "record `%s`", record->name.text));
}

/* Checks an object's methods. */
static void check_object(struct checker *checker, struct object *object) {
    struct names names = {0};
    for (struct function *method = object->methods; method != NULL; method = method->next) {
        add_name(checker, &names, &method->name);
        check_function(checker, method, object->group, NULL);
    }
    check_unique(checker, &names, arena_format(checker->arena, "object `%s`", object->name.text));
}

/* Checks a group's declarations, once every group's scope is complete. */
static void check_group(struct checker *checker, const struct group *group) {
    struct names names = {0};
    const struct scope *scope = &checker->scopes[group->index];
    for (size_t i = 0; i < scope->count; i++) {
        add_name(checker, &names, scope->entries[i].name);
    }
    check_unique(checker, &names, arena_format(checker->arena, "group `%s`", group->name.text));

    for (const struct item *item = group->items; item != NULL; item = item->next) {
        switch (item->kind) {
        case ITEM_RECORD:
            check_record(checker, item->as.record);
            break;
        case ITEM_OBJECT:
            check_object(checker, item->as.object);
            break;
        case ITEM_FUNCTION:
            check_function(checker, item->as.function, group, NULL);
            break;
        case ITEM_USE:
            break;
        }
    }
}

/* Checks a kind of script: its `uses`, and its entry points. */
static void check_script(struct checker *checker, struct script *script) {
    struct names names = {0};
    for (struct script_use *use = script->uses; use != NULL; use = use->next) {
        add_name(checker, &names, &use->name);
        use->group = find_group(checker->interface, use->name.text);
        if (use->group == NULL) {
            report_no_group(checker, &use->name);
        }
    }
    check_unique(checker, &names,
                 arena_format(checker->arena, "the `uses` of `%s`", script->name.text));

    names.count = 0;
    for (struct function *entry_point = script->entry_points; entry_point != NULL;
         entry_point = entry_point->next) {
        add_name(checker, &names, &entry_point->name);
        check_function(checker, entry_point, NULL, script);
    }
    check_unique(checker, &names, arena_format(checker->arena, "script `%s`", script->name.text));
}

/* Checks the names of the groups and kinds of script, and that there's exactly one kind of
 * script. */
static void check_top_level(struct checker *checker) {
    const struct interface *interface = checker->interface;
    struct names names = {0};
    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        add_name(checker, &names, &group->name);
    }
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        add_name(checker, &names, &script->name);
    }
    check_unique(checker, &names, "the interface");

    if (interface->scripts == NULL) {
        report_error(checker->diagnostics, interface->position,
                     "the interface declares no kind of script, but it needs exactly one");
    }
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        if (script != interface->scripts) {
            report_error(checker->diagnostics, script->name.position,
                         "another kind of script, but an interface has exactly one");
            report_note(checker->diagnostics, interface->scripts->name.position,
                        "the first is `%s`, here", interface->scripts->name.text);
        }
    }

    if (interface->space.text != NULL && strcmp(interface->space.text, "stasis") == 0) {
        report_error(checker->diagnostics, interface->space.position,
                     "the namespace `stasis` is reserved for Stasis's own interfaces");
    }
}

/* Where a depth-first search stands with a group or a record. */
enum search_state {
    /* The search hasn't reached it. */
    UNREACHED,
    /* The search is within it, which is on the search's stack. */
    WITHIN,
    /* The search has left it, and everything that it leads to. */
    LEFT,
};

/* A step of the search for groups that use each other in a cycle: a group, and the next of its
 * items to follow. */
struct group_step {
    const struct group *group;
    const struct item *next;
    /* The `use` the search followed from the group last. */
    const struct use *use;
};

/* Starts the step that enters `group`. */
static void enter_group(struct group_step *step, const struct group *group,
                        enum search_state *states) {
    step->group = group;
    step->next = group->items;
    step->use = NULL;
    states[group->index] = WITHIN;
}

/* Reports a cycle of `use`s, which runs from the group of steps[first] through each step after it,
 * and back. */
static void report_use_cycle(struct checker *checker, const struct group_step *steps, size_t first,
                             size_t last) {
    const char *cycle = arena_format(checker->arena, "`%s`", steps[first].group->name.text);
    for (size_t i = first; i <= last; i++) {
        cycle =
            arena_format(checker->arena, "%s%s`%s`", cycle, i == first ? " uses " : ", which uses ",
                         steps[i].use->group->name.text);
    }
    report_error(checker->diagnostics, steps[first].use->group_name.position,
                 "groups can't use each other in a cycle, but %s", cycle);
    for (size_t i = first + 1; i <= last; i++) {
        report_note(checker->diagnostics, steps[i].use->group_name.position, "`%s` uses `%s` here",
                    steps[i].group->name.text, steps[i].use->group->name.text);
    }
}

/* Reports each cycle of groups that use each other, in a depth-first search from `root` that
 * doesn't enter groups it has left. `states` holds each group's state, by the group's index. */
static void search_groups(struct checker *checker, const struct group *root,
                          enum search_state *states, struct group_step *steps) {
    size_t depth = 1;
    enter_group(&steps[0], root, states);
    while (depth > 0) {
        struct group_step *step = &steps[depth - 1];
        while (step->next != NULL &&
               (step->next->kind != ITEM_USE || step->next->as.use->group == NULL)) {
            step->next = step->next->next;
        }
        if (step->next == NULL) {
            states[step->group->index] = LEFT;
            depth--;
            continue;
        }
        step->use = step->next->as.use;
        step->next = step->next->next;
        const struct group *used = step->use->group;
        if (states[used->index] == UNREACHED) {
            enter_group(&steps[depth++], used, states);
        } else if (states[used->index] == WITHIN) {
            size_t first = 0;
            while (first < depth - 1 && steps[first].group != used) {
                first++;
            }
            report_use_cycle(checker, steps, first, depth - 1);
        }
    }
}

/* Reports each cycle of groups that use each other. */
static void check_use_cycles(struct checker *checker) {
    const size_t count = checker->interface->group_count + 1;
    enum search_state *states = arena_allocate(checker->arena, count * sizeof *states);
    struct group_step *steps = arena_allocate(checker->arena, count * sizeof *steps);
    for (const struct group *root = checker->interface->groups; root != NULL; root = root->next) {
        if (states[root->index] == UNREACHED) {
            search_groups(checker, root, states, steps);
        }
    }
}

/* A step of the search for records that contain themselves: a record, and the next of its fields
 * to follow. */
struct record_step {
    const struct record *record;
    const struct field *next;
    /* The field the search followed from the record last. */
    const struct field *field;
};

/* Starts the step that enters `record`. */
static void enter_record(struct record_step *step, const struct record *record,
                         enum search_state *states) {
    step->record = record;
    step->next = record->fields;
    step->field = NULL;
    states[record->index] = WITHIN;
}

/* Reports a record that contains itself, through the fields from steps[first] to steps[last]. */
static void report_record_cycle(struct checker *checker, const struct record_step *steps,
                                size_t first, size_t last) {
    const struct record *record = steps[first].record;
    report_error(checker->diagnostics, record->name.position, "record `%s` contains itself",
                 record->name.text);
    for (size_t i = first; i <= last; i++) {
        report_note(checker->diagnostics, steps[i].field->name.position, "`%s.%s` has type `%s`",
                    steps[i].record->name.text, steps[i].field->name.text,
                    steps[i].field->type.record->name.text);
    }
}

/* Reports each record that contains itself, directly or through other records, in a depth-first
 * search from `root` that doesn't enter records it has left. `states` holds each record's state, by
 * the record's index. */
static void search_records(struct checker *checker, const struct record *root,
                           enum search_state *states, struct record_step *steps) {
    size_t depth = 1;
    enter_record(&steps[0], root, states);
    while (depth > 0) {
        struct record_step *step = &steps[depth - 1];
        while (step->next != NULL && step->next->type.kind != TYPE_RECORD) {
            step->next = step->next->next;
        }
        if (step->next == NULL) {
            states[step->record->index] = LEFT;
            depth--;
            continue;
        }
        step->field = step->next;
        step->next = step->next->next;
        const struct record *contained = step->field->type.record;
        if (states[contained->index] == UNREACHED) {
            enter_record(&steps[depth++], contained, states);
        } else if (states[contained->index] == WITHIN) {
            size_t first = 0;
            while (first < depth - 1 && steps[first].record != contained) {
                first++;
            }
            report_record_cycle(checker, steps, first, depth - 1);
        }
    }
}

/* Reports each record that contains itself. */
static void check_record_cycles(struct checker *checker) {
    const size_t count = checker->interface->record_count + 1;
    enum search_state *states = arena_allocate(checker->arena, count * sizeof *states);
    struct record_step *steps = arena_allocate(checker->arena, count * sizeof *steps);
    for (const struct group *group = checker->interface->groups; group != NULL;
         group = group->next) {
        for (const struct item *item = group->items; item != NULL; item = item->next) {
            if (item->kind == ITEM_RECORD && states[item->as.record->index] == UNREACHED) {
                search_records(checker, item->as.record, states, steps);
            }
        }
    }
}

void check_interface(struct interface *interface, struct arena *arena,
                     struct diagnostics *diagnostics) {
    struct checker checker;
    checker.interface = interface;
    checker.arena = arena;
    checker.diagnostics = diagnostics;
    checker.scopes = arena_allocate(arena, (interface->group_count + 1) * sizeof *checker.scopes);

    check_top_level(&checker);
    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        add_declarations(&checker, group);
    }
    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        for (const struct item *item = group->items; item != NULL; item = item->next) {
            if (item->kind == ITEM_USE) {
                resolve_use(&checker, group, item->as.use);
            }
        }
    }
    check_use_cycles(&checker);
    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        check_group(&checker, group);
    }
    for (struct script *script = interface->scripts; script != NULL; script = script->next) {
        check_script(&checker, script);
    }
    check_record_cycles(&checker);
}
