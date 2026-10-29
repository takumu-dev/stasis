#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "descriptors.h"
#include "host_functions.h"
#include "layout.h"
#include "message.h"
#include "module.h"
#include "names.h"
#include "objects.h"
#include "platform.h"
#include "signature.h"
#include "snapshottable.h"
#include "system_interface.h"
#include "wamr_frames.h"

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wasm_export.h>

/* The prefix of the exports that free an entry point's results, which the name of the entry
 * point's export follows. */
static const char post_return_prefix[] = "cabi_post_";

/* The room for WAMR's message when its loader refuses a module. */
enum { WAMR_MESSAGE_SIZE = 256 };

/* The room for a line as text, such as "0.4294967295", and its NUL. */
enum { LINE_SIZE = 24 };

/* The room for a core function type as text, such as "(i32, i64) -> (f32)", which is cut short if
 * it doesn't fit. */
enum { CORE_TYPE_SIZE = 128 };

/* How one of a script's import or export names parsed: whether it's one Stasis takes for a wire
 * name at all, and if so, whether it parsed, and as what. */
struct parsed_name {
    bool wire;
    bool parsed;
    struct wire_name name;
};

/* The exports that name one of the interface's entry points: how many there are, the first two,
 * and whether the first's hash is the host's. */
struct entry_point_exports {
    size_t count;
    const struct module_export *first;
    const struct module_export *second;
    bool same_signature;
};

/* What a function imported from one of the interface's groups names: whether its field name parsed,
 * and if so, as what, the host function or method it names, if the interface has it, and whether
 * its hash is the host's. */
struct host_import {
    bool from_group;
    bool parsed;
    struct field_name field;
    const stasis_host_function *function;
    bool same_signature;
};

/* A load in progress. */
struct load {
    const stasis_interface *interface_data;
    const stasis_script_kind *kind;
    struct message *message;
    struct module module;
    struct descriptor_walk walk;
    struct layout_builder layouts;
    /* How each import's module name and each export's name parsed, by index. */
    struct parsed_name *imports;
    struct parsed_name *exports;
    /* The exports that name each of the interface's entry points, by id. */
    struct entry_point_exports *entry_points;
    /* What each import names, if it's a function from one of the interface's groups, by index. */
    struct host_import *host_imports;
    /* The status of the first problem found, or STASIS_OK while there's none. */
    stasis_status status;
};

/* The length of a name as printf's precision takes it, which is all of it unless it's absurdly
 * long. */
static int printed(stasis_string text) { return text.size > INT_MAX ? INT_MAX : (int)text.size; }

/* Notes a problem: its line in the message, and its status if it's the first. */
static void problem(struct load *load, stasis_status status, const char *format, ...)
    STASIS_PRINTF_FORMAT(3, 4);

static void problem(struct load *load, stasis_status status, const char *format, ...) {
    if (load->status == STASIS_OK) {
        load->status = status;
    }
    va_list arguments;
    va_start(arguments, format);
    stasis_message_add_v(load->message, format, arguments);
    va_end(arguments);
}

/* Parses each import's module name and each export's name that Stasis takes for a wire name. An
 * export that frees an entry point's results is left alone. */
static void parse_names(struct load *load) {
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        const stasis_string module = load->module.imports[i].module;
        struct parsed_name *parsed = &load->imports[i];
        parsed->wire = stasis_is_group_name(module);
        if (parsed->wire) {
            parsed->parsed = stasis_parse_group_name(module, &parsed->name);
        }
        struct host_import *host = &load->host_imports[i];
        if (parsed->parsed && load->module.imports[i].kind == MODULE_FUNCTION) {
            host->from_group = true;
            host->parsed = stasis_parse_field_name(load->module.imports[i].field, &host->field);
        }
    }
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        const stasis_string name = load->module.exports[i].name;
        struct parsed_name *parsed = &load->exports[i];
        if (name.size >= sizeof post_return_prefix - 1 &&
            memcmp(name.data, post_return_prefix, sizeof post_return_prefix - 1) == 0) {
            continue;
        }
        parsed->wire = stasis_is_entry_point_name(name);
        if (parsed->wire) {
            parsed->parsed = stasis_parse_entry_point_name(name, &parsed->name);
        }
    }
}

/* Whether a parsed name names an interface other than the host's. */
static bool other_interface(const struct load *load, const struct wire_name *name) {
    if (!stasis_string_is(name->space, load->interface_data->space)) {
        return true;
    }
    if (!stasis_string_is(name->name, load->interface_data->name)) {
        return true;
    }
    return false;
}

/* Whether a parsed name names another line of the host's interface than the host's. */
static bool other_line(const struct load *load, const struct wire_name *name) {
    if (load->interface_data->major == 0) {
        if (name->line.major != 0) {
            return true;
        }
        return name->line.minor != load->interface_data->minor;
    }
    return name->line.major != load->interface_data->major;
}

/* Picks out one of the script's names that parsed. */
typedef bool name_test(const struct load *load, const struct wire_name *name);

/* The first name that parsed and that `test` picks out, import modules before exports, or null if
 * there's none. `text` is set to the name, and `export` to whether it's an export's. */
static const struct parsed_name *first_name(const struct load *load, name_test *test,
                                            const stasis_string **text, bool *export) {
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        if (load->imports[i].parsed && test(load, &load->imports[i].name)) {
            *text = &load->module.imports[i].module;
            *export = false;
            return &load->imports[i];
        }
    }
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        if (load->exports[i].parsed && test(load, &load->exports[i].name)) {
            *text = &load->module.exports[i].name;
            *export = true;
            return &load->exports[i];
        }
    }
    return NULL;
}

/* The interface's full name and version, as in `acme:dungeon@1.2.0`, for a message. */
#define INTERFACE_FORMAT "`%s:%s@%lu.%lu.%lu`"
#define INTERFACE_ARGUMENTS(interface_data)                                                        \
    (interface_data)->space, (interface_data)->name, (unsigned long)(interface_data)->major,       \
        (unsigned long)(interface_data)->minor, (unsigned long)(interface_data)->patch

/* A host function's or a method's name as an import names it, with its group, as in `world.find`
 * or `world.entity.name`, for a message. */
#define HOST_FUNCTION_FORMAT "`%.*s.%.*s%s%.*s`"
#define HOST_FUNCTION_ARGUMENTS(group, field)                                                      \
    printed(group), (group).data, printed((field).object), (field).object.data,                    \
        (field).object.size > 0 ? "." : "", printed((field).function), (field).function.data

/* The stages that stop the load at the first that fails: every name must be the host's interface,
 * and then its line. Returns false if one fails. */
static bool check_stages(struct load *load) {
    const stasis_string *text = NULL;
    bool export = false;
    const struct parsed_name *other = first_name(load, other_interface, &text, &export);
    if (other != NULL) {
        problem(load, STASIS_WRONG_INTERFACE,
                "the script was built for the interface `%.*s:%.*s`, not " INTERFACE_FORMAT
                ", as its %s `%.*s` says",
                printed(other->name.space), other->name.space.data, printed(other->name.name),
                other->name.name.data, INTERFACE_ARGUMENTS(load->interface_data),
                export ? "export" : "import module", printed(*text), text->data);
        return false;
    }
    other = first_name(load, other_line, &text, &export);
    if (other != NULL) {
        char line[LINE_SIZE];
        if (other->name.line.major == 0) {
            (void)snprintf(line, sizeof line, "0.%lu", (unsigned long)other->name.line.minor);
        } else {
            (void)snprintf(line, sizeof line, "%lu", (unsigned long)other->name.line.major);
        }
        problem(load, STASIS_INCOMPATIBLE_VERSION,
                "the script was built for `%s:%s@%s`, which isn't compatible with " INTERFACE_FORMAT
                ", as its %s `%.*s` says",
                load->interface_data->space, load->interface_data->name, line,
                INTERFACE_ARGUMENTS(load->interface_data), export ? "export" : "import module",
                printed(*text), text->data);
        return false;
    }
    return true;
}

/* Finds the exports that name each of the interface's entry points. An export of another kind of
 * script, or of an entry point the interface lacks, names none of them. */
static void match_entry_points(struct load *load) {
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        const struct parsed_name *parsed = &load->exports[i];
        if (!parsed->parsed || !stasis_string_is(parsed->name.scope, load->kind->name)) {
            continue;
        }
        for (size_t id = 0; id < load->kind->entry_point_count; id++) {
            if (stasis_string_is(parsed->name.entry_point, load->kind->entry_points[id].name)) {
                struct entry_point_exports *exports = &load->entry_points[id];
                if (exports->count == 0) {
                    exports->first = &load->module.exports[i];
                } else if (exports->count == 1) {
                    exports->second = &load->module.exports[i];
                }
                exports->count++;
                break;
            }
        }
    }
}

/* The group of the interface called `name`, with its index in `index`, or null if it has none. */
static const stasis_group *find_group(const stasis_interface *interface_data, stasis_string name,
                                      size_t *index) {
    for (size_t g = 0; g < interface_data->group_count; g++) {
        if (stasis_string_is(name, interface_data->groups[g].name)) {
            *index = g;
            return &interface_data->groups[g];
        }
    }
    return NULL;
}

/* The host function or the method that `field` names among those of the group called `group`, or
 * null if the interface has none. */
static const stasis_host_function *find_host_function(const stasis_interface *interface_data,
                                                      stasis_string group,
                                                      const struct field_name *field) {
    size_t index = 0;
    const stasis_group *found = find_group(interface_data, group, &index);
    if (found == NULL) {
        return NULL;
    }
    const stasis_host_function *functions = found->functions;
    size_t count = found->function_count;
    if (field->object.size > 0) {
        functions = NULL;
        count = 0;
        for (size_t k = 0; k < interface_data->kind_count; k++) {
            const stasis_object_kind *kind = &interface_data->kinds[k];
            if (kind->group == index && stasis_string_is(field->object, kind->name)) {
                functions = kind->methods;
                count = kind->method_count;
                break;
            }
        }
    }
    for (size_t i = 0; i < count; i++) {
        if (stasis_string_is(field->function, functions[i].name)) {
            return &functions[i];
        }
    }
    return NULL;
}

/* Finds the host function or method that each function imported from a group names. */
static void match_host_functions(struct load *load) {
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        struct host_import *host = &load->host_imports[i];
        if (host->parsed) {
            host->function =
                find_host_function(load->interface_data, load->imports[i].name.scope, &host->field);
        }
    }
}

/* The problems with the script's host functions: one the interface lacks, and one whose hash isn't
 * the host's. Returns false if the interface's descriptors are malformed. */
static bool check_host_function_signatures(struct load *load) {
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        struct host_import *host = &load->host_imports[i];
        if (!host->parsed) {
            continue;
        }
        const stasis_string group = load->imports[i].name.scope;
        if (host->function == NULL) {
            problem(load, STASIS_INTERFACE_MISMATCH,
                    "the script imports the host function " HOST_FUNCTION_FORMAT
                    ", which " INTERFACE_FORMAT " lacks",
                    HOST_FUNCTION_ARGUMENTS(group, host->field),
                    INTERFACE_ARGUMENTS(load->interface_data));
            continue;
        }
        char hash[STASIS_SIGNATURE_HASH_LENGTH + 1];
        if (!stasis_host_function_hash(&load->walk, host->function, hash)) {
            return false;
        }
        host->same_signature = stasis_string_is(host->field.hash, hash);
        if (!host->same_signature) {
            problem(load, STASIS_INTERFACE_MISMATCH,
                    "the script's host function " HOST_FUNCTION_FORMAT
                    " has another signature than in " INTERFACE_FORMAT,
                    HOST_FUNCTION_ARGUMENTS(group, host->field),
                    INTERFACE_ARGUMENTS(load->interface_data));
        }
    }
    return true;
}

/* The hash in the name of an export that parsed as an entry point's. */
static stasis_string export_hash(const struct load *load, const struct module_export *export) {
    return load->exports[export - load->module.exports].name.hash;
}

/* The problems with the script's signatures: a required entry point it lacks, an entry point whose
 * hash isn't the host's, and then its host functions'. Returns false if the interface's descriptors
 * are malformed. */
static bool check_signatures(struct load *load) {
    for (size_t id = 0; id < load->kind->entry_point_count; id++) {
        const stasis_entry_point *entry_point = &load->kind->entry_points[id];
        struct entry_point_exports *exports = &load->entry_points[id];
        char hash[STASIS_SIGNATURE_HASH_LENGTH + 1];
        if (!stasis_entry_point_hash(&load->walk, entry_point, hash)) {
            return false;
        }
        if (exports->count == 1) {
            exports->same_signature = stasis_string_is(export_hash(load, exports->first), hash);
        }
        if (exports->count == 0 && !entry_point->optional) {
            problem(load, STASIS_INTERFACE_MISMATCH,
                    "the script lacks the entry point `%s.%s`, which " INTERFACE_FORMAT " requires",
                    load->kind->name, entry_point->name, INTERFACE_ARGUMENTS(load->interface_data));
        } else if (exports->count == 1 && !exports->same_signature) {
            problem(
                load, STASIS_INTERFACE_MISMATCH,
                "the script's entry point `%s.%s` has another signature than in " INTERFACE_FORMAT,
                load->kind->name, entry_point->name, INTERFACE_ARGUMENTS(load->interface_data));
        }
    }
    return check_host_function_signatures(load);
}

/* The problems with the script's names: names that don't parse, entry points of more than one kind
 * of script, and an entry point exported twice. */
static void check_names(struct load *load) {
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        const stasis_string module = load->module.imports[i].module;
        if (load->imports[i].wire && !load->imports[i].parsed) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the import module `%.*s` doesn't parse as the name of a group of an "
                    "interface",
                    printed(module), module.data);
        } else if (load->host_imports[i].from_group && !load->host_imports[i].parsed) {
            const stasis_string field = load->module.imports[i].field;
            problem(load, STASIS_INVALID_SCRIPT,
                    "the import `%.*s` from `%.*s` doesn't parse as the name of a host function",
                    printed(field), field.data, printed(module), module.data);
        }
    }
    const struct wire_name *first_kind = NULL;
    bool kinds_reported = false;
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        const struct parsed_name *parsed = &load->exports[i];
        if (parsed->wire && !parsed->parsed) {
            const stasis_string name = load->module.exports[i].name;
            problem(load, STASIS_INVALID_SCRIPT,
                    "the export `%.*s` doesn't parse as the name of an entry point", printed(name),
                    name.data);
            continue;
        }
        if (!parsed->parsed) {
            continue;
        }
        if (first_kind == NULL) {
            first_kind = &parsed->name;
        } else if (!kinds_reported && (parsed->name.scope.size != first_kind->scope.size ||
                                       memcmp(parsed->name.scope.data, first_kind->scope.data,
                                              first_kind->scope.size) != 0)) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script's entry points are of more than one kind of script: `%.*s` and "
                    "`%.*s`",
                    printed(first_kind->scope), first_kind->scope.data, printed(parsed->name.scope),
                    parsed->name.scope.data);
            kinds_reported = true;
        }
    }
    for (size_t id = 0; id < load->kind->entry_point_count; id++) {
        const struct entry_point_exports *exports = &load->entry_points[id];
        if (exports->count > 1) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script exports the entry point `%s.%s` more than once, as `%.*s` and "
                    "`%.*s`",
                    load->kind->name, load->kind->entry_points[id].name,
                    printed(exports->first->name), exports->first->name.data,
                    printed(exports->second->name), exports->second->name.data);
        }
    }
}

/* Adds as much of `text` as fits to the string of `*length` bytes in the `size` bytes at
 * `buffer`. */
static void append(char *buffer, size_t size, size_t *length, const char *text) {
    size_t added = strlen(text);
    if (added > size - 1 - *length) {
        added = size - 1 - *length;
    }
    memcpy(buffer + *length, text, added);
    *length += added;
    buffer[*length] = '\0';
}

/* Adds a list of core value types, such as `(i32, i64)`. */
static void append_types(char *buffer, size_t size, size_t *length, const uint8_t *types,
                         uint32_t count) {
    append(buffer, size, length, "(");
    for (uint32_t i = 0; i < count; i++) {
        if (i > 0) {
            append(buffer, size, length, ", ");
        }
        append(buffer, size, length, stasis_module_type_name(types[i]));
    }
    append(buffer, size, length, ")");
}

/* Writes a core function type, such as `(i32, i64) -> (f32)`, into the `size` bytes at `text`,
 * which is at least 1. */
static void format_type(char *text, size_t size, const uint8_t *parameters,
                        uint32_t parameter_count, const uint8_t *results, uint32_t result_count) {
    size_t length = 0;
    text[0] = '\0';
    append_types(text, size, &length, parameters, parameter_count);
    append(text, size, &length, " -> ");
    append_types(text, size, &length, results, result_count);
}

/* Whether a function's type is the core type an entry point flattens to. */
static bool has_core_type(const struct module_function_type *type, const struct core_type *core) {
    if (type->parameter_count != core->parameter_count ||
        type->result_count != core->result_count) {
        return false;
    }
    if (memcmp(type->parameters, core->parameters, core->parameter_count) != 0) {
        return false;
    }
    return memcmp(type->results, core->results, core->result_count) == 0;
}

/* Writes a function's type, and the core type it should have been, as text, as in
 * `(i32, i64) -> (f32)`, into `actual` and `expected_text`, which hold CORE_TYPE_SIZE bytes
 * each. */
static void format_types(char *actual, char *expected_text, const struct module_function_type *type,
                         const struct core_type *expected) {
    format_type(actual, CORE_TYPE_SIZE, type->parameters, type->parameter_count, type->results,
                type->result_count);
    format_type(expected_text, CORE_TYPE_SIZE, expected->parameters, expected->parameter_count,
                expected->results, expected->result_count);
}

/* The type of a function that the script imports, or null if the module has no such type. */
static const struct module_function_type *import_type(const struct load *load,
                                                      const struct module_import *import) {
    return import->type < load->module.type_count ? &load->module.types[import->type] : NULL;
}

/* The problems with the exports Stasis calls: an entry point or stasis_initialize that isn't a
 * function, or whose type isn't the one Stasis calls it with. Returns false if the interface's
 * descriptors are malformed. */
static bool check_exports(struct load *load) {
    for (size_t id = 0; id < load->kind->entry_point_count; id++) {
        const stasis_entry_point *entry_point = &load->kind->entry_points[id];
        const struct entry_point_exports *exports = &load->entry_points[id];
        struct core_type core;
        if (!stasis_entry_point_core_type(&load->walk, entry_point, &core)) {
            return false;
        }
        /* A signature that differs already fails the load, and its export's type is what that
         * signature makes it. */
        if (exports->count != 1 || !exports->same_signature) {
            continue;
        }
        const struct module_export *export = exports->first;
        if (export->kind != MODULE_FUNCTION) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script's entry point `%s.%s` is exported as something other than a "
                    "function",
                    load->kind->name, entry_point->name);
            continue;
        }
        const struct module_function_type *type =
            stasis_module_function_type(&load->module, export->index);
        if (type != NULL && !has_core_type(type, &core)) {
            char actual[CORE_TYPE_SIZE];
            char expected[CORE_TYPE_SIZE];
            format_types(actual, expected, type, &core);
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script's entry point `%s.%s` is a function of type %s, where its "
                    "signature makes it %s",
                    load->kind->name, entry_point->name, actual, expected);
        }
    }
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        const struct module_export *export = &load->module.exports[i];
        if (!stasis_string_is(export->name, STASIS_INITIALIZE_NAME)) {
            continue;
        }
        const struct module_function_type *type =
            export->kind == MODULE_FUNCTION
                ? stasis_module_function_type(&load->module, export->index)
                : NULL;
        if (export->kind != MODULE_FUNCTION ||
            (type != NULL && (type->parameter_count != 0 || type->result_count != 0))) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script's `%s` isn't a function that takes and returns nothing",
                    STASIS_INITIALIZE_NAME);
        }
    }
    return true;
}

/* The problems with the host functions the script imports: one whose type isn't the one Stasis
 * calls it with. Returns false if the interface's descriptors are malformed. */
static bool check_imports(struct load *load) {
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        const struct host_import *host = &load->host_imports[i];
        /* A signature that differs already fails the load, and the import's type is what that
         * signature makes it. */
        if (host->function == NULL || !host->same_signature) {
            continue;
        }
        struct core_type core;
        if (!stasis_host_function_core_type(&load->walk, host->function, &core)) {
            return false;
        }
        const struct module_function_type *type = import_type(load, &load->module.imports[i]);
        if (type != NULL && !has_core_type(type, &core)) {
            char actual[CORE_TYPE_SIZE];
            char expected[CORE_TYPE_SIZE];
            format_types(actual, expected, type, &core);
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script imports the host function " HOST_FUNCTION_FORMAT
                    " as a function of type %s, where its signature makes it %s",
                    HOST_FUNCTION_ARGUMENTS(load->imports[i].name.scope, host->field), actual,
                    expected);
        }
    }
    return true;
}

/* The export that frees the results of the entry point exported as `name`, or null if the script
 * doesn't export one. */
static const struct module_export *find_post_return(const struct load *load, stasis_string name) {
    const size_t prefix_size = sizeof post_return_prefix - 1;
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        const stasis_string export_name = load->module.exports[i].name;
        if (export_name.size == prefix_size + name.size &&
            memcmp(export_name.data, post_return_prefix, prefix_size) == 0 &&
            memcmp(export_name.data + prefix_size, name.data, name.size) == 0) {
            return &load->module.exports[i];
        }
    }
    return NULL;
}

/* The core function type of cabi_realloc, as the canonical ABI types it. */
static const struct core_type realloc_type = {
    .parameters = {MODULE_I32, MODULE_I32, MODULE_I32, MODULE_I32},
    .parameter_count = 4,
    .results = {MODULE_I32},
    .result_count = 1,
};

/* How an export compares with the function Stasis calls it as. */
enum export_type {
    EXPORT_EXPECTED,
    EXPORT_NOT_A_FUNCTION,
    EXPORT_OF_OTHER_TYPE,
};

/* Compares an export with a function of the core type `expected`. When it's a function of another
 * type, writes its type and `expected` as text, as in `(i32, i64) -> (f32)`, into `actual` and
 * `expected_text`, which hold CORE_TYPE_SIZE bytes each. */
static enum export_type compare_type(const struct load *load, const struct module_export *export,
                                     const struct core_type *expected, char *actual,
                                     char *expected_text) {
    if (export->kind != MODULE_FUNCTION) {
        return EXPORT_NOT_A_FUNCTION;
    }
    const struct module_function_type *type =
        stasis_module_function_type(&load->module, export->index);
    if (type == NULL || has_core_type(type, expected)) {
        return EXPORT_EXPECTED;
    }
    format_types(actual, expected_text, type, expected);
    return EXPORT_OF_OTHER_TYPE;
}

/* Notes the problem with an export that isn't a function. */
static void not_a_function(struct load *load, const struct module_export *export) {
    problem(load, STASIS_INVALID_SCRIPT,
            "the script's `%.*s` is exported as something other than a function",
            printed(export->name), export->name.data);
}

/* Sets `holds` to whether any of the `count` values at `values` holds a string. Returns false if
 * their descriptors are malformed. */
static bool any_holds_string(struct load *load, const stasis_value *values, size_t count,
                             bool *holds) {
    *holds = false;
    for (size_t i = 0; i < count && !*holds; i++) {
        if (!stasis_holds_string(&load->walk, &values[i], holds)) {
            return false;
        }
    }
    return true;
}

/* The problem with cabi_realloc, if the script lacks it though Stasis needs it to put the result of
 * a host function it imports, or the arguments of an entry point it provides, into its memory,
 * which it does for each that holds a string, and for arguments that spill. Returns false if the
 * interface's descriptors are malformed. */
static bool check_realloc_needed(struct load *load) {
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        if (stasis_string_is(load->module.exports[i].name, STASIS_REALLOC_NAME)) {
            return true;
        }
    }
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        const struct host_import *host = &load->host_imports[i];
        if (host->function == NULL || !host->same_signature || host->function->result == NULL) {
            continue;
        }
        bool holds = false;
        if (!any_holds_string(load, host->function->result, 1, &holds)) {
            return false;
        }
        if (holds) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script lacks the export `" STASIS_REALLOC_NAME "`, which Stasis needs to "
                    "put the result of the host function " HOST_FUNCTION_FORMAT " into its memory",
                    HOST_FUNCTION_ARGUMENTS(load->imports[i].name.scope, host->field));
            return true;
        }
    }
    for (size_t id = 0; id < load->kind->entry_point_count; id++) {
        const stasis_entry_point *entry_point = &load->kind->entry_points[id];
        if (load->entry_points[id].count != 1 || !load->entry_points[id].same_signature) {
            continue;
        }
        bool holds = false;
        size_t flat_count = 0;
        if (!any_holds_string(load, entry_point->parameters, entry_point->parameter_count,
                              &holds) ||
            !stasis_flat_count(&load->walk, entry_point->parameters, entry_point->parameter_count,
                               &flat_count)) {
            return false;
        }
        if (holds || flat_count > STASIS_MAX_FLAT_PARAMETERS) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script lacks the export `" STASIS_REALLOC_NAME
                    "`, which Stasis needs to put the arguments of the entry point `%s.%s` into "
                    "its memory",
                    load->kind->name, entry_point->name);
            return true;
        }
    }
    return true;
}

/* The problems with the exports of the canonical ABI's that Stasis calls: cabi_realloc, which must
 * be there if Stasis needs it, and of the canonical ABI's type, and the cabi_post_* of each entry
 * point, which takes the entry point's core results. Returns false if the interface's descriptors
 * are malformed. */
static bool check_abi_exports(struct load *load) {
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        const struct module_export *export = &load->module.exports[i];
        if (!stasis_string_is(export->name, STASIS_REALLOC_NAME)) {
            continue;
        }
        char actual[CORE_TYPE_SIZE];
        char expected[CORE_TYPE_SIZE];
        switch (compare_type(load, export, &realloc_type, actual, expected)) {
        case EXPORT_EXPECTED:
            break;
        case EXPORT_NOT_A_FUNCTION:
            not_a_function(load, export);
            break;
        case EXPORT_OF_OTHER_TYPE:
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script's `" STASIS_REALLOC_NAME
                    "` is a function of type %s, where Stasis calls it as %s",
                    actual, expected);
            break;
        }
    }
    for (size_t id = 0; id < load->kind->entry_point_count; id++) {
        const stasis_entry_point *entry_point = &load->kind->entry_points[id];
        const struct entry_point_exports *exports = &load->entry_points[id];
        if (exports->count != 1 || !exports->same_signature) {
            continue;
        }
        const struct module_export *post_return = find_post_return(load, exports->first->name);
        if (post_return == NULL) {
            continue;
        }
        struct core_type entry_type;
        if (!stasis_entry_point_core_type(&load->walk, entry_point, &entry_type)) {
            return false;
        }
        struct core_type post_type;
        memset(&post_type, 0, sizeof post_type);
        memcpy(post_type.parameters, entry_type.results, entry_type.result_count);
        post_type.parameter_count = entry_type.result_count;
        char actual[CORE_TYPE_SIZE];
        char expected[CORE_TYPE_SIZE];
        switch (compare_type(load, post_return, &post_type, actual, expected)) {
        case EXPORT_EXPECTED:
            break;
        case EXPORT_NOT_A_FUNCTION:
            not_a_function(load, post_return);
            break;
        case EXPORT_OF_OTHER_TYPE:
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script's `%.*s` is a function of type %s, where the results of the entry "
                    "point `%s.%s` make it %s",
                    printed(post_return->name), post_return->name.data, actual, load->kind->name,
                    entry_point->name, expected);
            break;
        }
    }
    return check_realloc_needed(load);
}

/* Whether an export's name is that of a function some backend runs by itself when it creates an
 * instance, as it would a start section: WAMR runs `__wasm_call_ctors` and `__post_instantiate`,
 * and with its own system interface, `_initialize`. */
static bool runs_by_itself(stasis_string name) {
    static const char *const names[] = {"_initialize", "__wasm_call_ctors", "__post_instantiate"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (stasis_string_is(name, names[i])) {
            return true;
        }
    }
    return false;
}

/* How a message names a kind of import. */
static const char *import_kind_name(uint8_t kind) {
    switch (kind) {
    case MODULE_TABLE:
        return "table";
    case MODULE_MEMORY:
        return "memory";
    case MODULE_GLOBAL:
        return "global";
    default:
        return "function";
    }
}

/* Notes a problem if an active segment, number `number` among those of its `kind`, ends past
 * `size`, the number of `units` that the `target` it's copied into starts with. */
static void check_fit(struct load *load, const char *kind, uint32_t number,
                      const struct module_segment *segment, const char *target, uint64_t size,
                      const char *units) {
    const uint64_t end = (uint64_t)segment->offset + segment->size;
    if (end > size) {
        problem(load, STASIS_INVALID_SCRIPT,
                "the script's %s segment %lu needs its %s %lu to hold %llu %s, but it starts with "
                "%llu",
                kind, (unsigned long)number, target, (unsigned long)segment->index,
                (unsigned long long)end, units, (unsigned long long)size);
    }
}

/* The problems with the active segments, each of which must fit the memory or the table it's copied
 * into as that starts, or instantiating the script would trap. A segment of an imported memory or
 * table, or whose offset reads an imported global, is left alone: a script may import neither. */
static void check_segments(struct load *load) {
    const struct module *module = &load->module;
    for (uint32_t i = 0; i < module->data_segment_count; i++) {
        const struct module_segment *segment = &module->data_segments[i];
        const struct module_memory *memory = stasis_module_memory(module, segment->index);
        if (segment->active && segment->constant_offset && memory != NULL) {
            check_fit(load, "data", i, segment, "memory",
                      (uint64_t)memory->minimum * MODULE_PAGE_SIZE, "bytes");
        }
    }
    for (uint32_t i = 0; i < module->element_segment_count; i++) {
        const struct module_segment *segment = &module->element_segments[i];
        const struct module_table *table = stasis_module_table(module, segment->index);
        if (segment->active && segment->constant_offset && table != NULL) {
            check_fit(load, "element", i, segment, "table", table->minimum, "elements");
        }
    }
}

/* Notes the problem with a function that the script imports from the system interface, if the
 * system interface lacks it, or has it with another type. */
static void check_system_import(struct load *load, const struct module_import *import) {
    struct core_type expected;
    if (!stasis_system_function_type(import->field, &expected)) {
        problem(load, STASIS_INVALID_SCRIPT,
                "the script imports `%.*s` from `" STASIS_SYSTEM_INTERFACE_MODULE
                "`, which the system interface lacks",
                printed(import->field), import->field.data);
        return;
    }
    const struct module_function_type *type = import_type(load, import);
    if (type == NULL || has_core_type(type, &expected)) {
        return;
    }
    char actual[CORE_TYPE_SIZE];
    char expected_text[CORE_TYPE_SIZE];
    format_types(actual, expected_text, type, &expected);
    problem(load, STASIS_INVALID_SCRIPT,
            "the script imports `%.*s` from `" STASIS_SYSTEM_INTERFACE_MODULE
            "` as a function of type %s, where the system interface's is %s",
            printed(import->field), import->field.data, actual, expected_text);
}

/* The problems with what the script holds, which break Stasis's rules for scripts: a start
 * section, an export a backend runs by itself, an import that isn't a function, one from a module
 * other than the interface's groups and the system interface, a function of the system
 * interface's that it lacks or has with another type, an active segment that doesn't fit, and a
 * memory that starts larger than an instance may have. */
static void check_rules(struct load *load) {
    if (load->module.has_start) {
        problem(load, STASIS_INVALID_SCRIPT,
                "the script has a start section, which a backend runs whenever it creates an "
                "instance");
    }
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        const stasis_string name = load->module.exports[i].name;
        if (runs_by_itself(name)) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script exports `%.*s`, which a backend runs by itself when it creates an "
                    "instance",
                    printed(name), name.data);
        }
    }
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        const struct module_import *import = &load->module.imports[i];
        if (import->kind != MODULE_FUNCTION) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script imports the %s `%.*s` from `%.*s`, where a script may import only "
                    "functions",
                    import_kind_name(import->kind), printed(import->field), import->field.data,
                    printed(import->module), import->module.data);
        }
    }
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        const struct module_import *import = &load->module.imports[i];
        if (!load->imports[i].wire &&
            !stasis_string_is(import->module, STASIS_SYSTEM_INTERFACE_MODULE)) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script imports `%.*s` from `%.*s`, which is neither a group "
                    "of " INTERFACE_FORMAT " nor `" STASIS_SYSTEM_INTERFACE_MODULE "`",
                    printed(import->field), import->field.data, printed(import->module),
                    import->module.data, INTERFACE_ARGUMENTS(load->interface_data));
        }
    }
    for (uint32_t i = 0; i < load->module.import_count; i++) {
        const struct module_import *import = &load->module.imports[i];
        if (import->kind == MODULE_FUNCTION &&
            stasis_string_is(import->module, STASIS_SYSTEM_INTERFACE_MODULE)) {
            check_system_import(load, import);
        }
    }
    check_segments(load);
    for (uint32_t i = 0; i < load->module.memory_count; i++) {
        const uint32_t index = load->module.memory_import_count + i;
        const uint32_t minimum = load->module.memories[i].minimum;
        if (minimum > STASIS_MAX_MEMORY_PAGES) {
            problem(load, STASIS_INVALID_SCRIPT,
                    "the script's memory %lu starts with %lu pages, more than the %lu an instance "
                    "may have",
                    (unsigned long)index, (unsigned long)minimum,
                    (unsigned long)STASIS_MAX_MEMORY_PAGES);
        }
    }
}

/* Runs the load check on a module that WAMR has found valid. Returns STASIS_OK, the status of the
 * first problem, or STASIS_INVALID_ARGUMENT if the interface's descriptors are malformed. */
static stasis_status check(struct load *load) {
    load->imports = stasis_allocate_zeroed(load->module.import_count, sizeof *load->imports);
    load->exports = stasis_allocate_zeroed(load->module.export_count, sizeof *load->exports);
    load->entry_points =
        stasis_allocate_zeroed(load->kind->entry_point_count, sizeof *load->entry_points);
    load->host_imports =
        stasis_allocate_zeroed(load->module.import_count, sizeof *load->host_imports);
    if (load->imports == NULL || load->exports == NULL || load->entry_points == NULL ||
        load->host_imports == NULL) {
        return STASIS_OUT_OF_MEMORY;
    }
    parse_names(load);
    if (!check_stages(load)) {
        return load->status;
    }
    match_entry_points(load);
    match_host_functions(load);
    if (!check_signatures(load)) {
        return STASIS_INVALID_ARGUMENT;
    }
    check_names(load);
    if (!check_exports(load) || !check_imports(load) || !check_abi_exports(load)) {
        return STASIS_INVALID_ARGUMENT;
    }
    check_rules(load);
    return load->status;
}

/* Frees what a load allocated for itself. */
static void free_load(struct load *load) {
    stasis_module_free(&load->module);
    stasis_layout_builder_free(&load->layouts);
    stasis_walk_free(&load->walk);
    stasis_deallocate(load->imports);
    stasis_deallocate(load->exports);
    stasis_deallocate(load->entry_points);
    stasis_deallocate(load->host_imports);
}

/* Copies a name into a C string of its own, or returns null if there's no memory for it. */
static char *copy_name(stasis_string name) {
    char *copy = stasis_allocate(name.size + 1);
    if (copy != NULL) {
        if (name.size > 0) {
            memcpy(copy, name.data, name.size);
        }
        copy[name.size] = '\0';
    }
    return copy;
}

/* Frees a script's own memory, once its module has been unloaded. */
static void free_script(stasis_script *script) {
    if (script->entry_points != NULL) {
        for (size_t id = 0; id < script->entry_point_count; id++) {
            stasis_deallocate(script->entry_points[id]);
        }
    }
    if (script->post_returns != NULL) {
        for (size_t id = 0; id < script->entry_point_count; id++) {
            stasis_deallocate(script->post_returns[id]);
        }
    }
    stasis_deallocate((void *)script->entry_points);
    stasis_deallocate((void *)script->post_returns);
    if (script->entry_point_layouts != NULL) {
        for (size_t id = 0; id < script->entry_point_count; id++) {
            stasis_function_layout_free(&script->entry_point_layouts[id]);
        }
    }
    stasis_deallocate(script->entry_point_layouts);
    stasis_host_functions_free(&script->host_functions);
    stasis_deallocate(script->unsnapshottable);
    if (script->global_names != NULL) {
        for (size_t i = 0; i < script->snapshot_rules.global_count; i++) {
            stasis_deallocate(script->global_names[i]);
        }
    }
    stasis_deallocate((void *)script->global_names);
    stasis_deallocate(script->globals);
    stasis_module_code_free(&script->code);
    stasis_deallocate(script->bytes);
    stasis_deallocate(script);
}

/* The name of the first export of the global at `index`, or an empty name if there's none. */
static stasis_string global_export_name(const struct module *module, uint32_t index) {
    for (uint32_t i = 0; i < module->export_count; i++) {
        const struct module_export *export = &module->exports[i];
        if (export->kind == MODULE_GLOBAL && export->index == index) {
            return export->name;
        }
    }
    const stasis_string none = {NULL, 0};
    return none;
}

/* Fills in what a snapshottable script's snapshots must agree with: the limits of its one memory,
 * which may grow no larger than an instance's may, and each mutable global it defines, which it
 * exports, with the name it's exported as. Returns false if there's no memory for it. */
static bool keep_snapshot_rules(stasis_script *script, const struct load *load) {
    const struct module *module = &load->module;
    size_t count = 0;
    for (uint32_t i = 0; i < module->global_count; i++) {
        if (module->globals[i].is_mutable) {
            count++;
        }
    }
    script->globals = stasis_allocate_zeroed(count, sizeof *script->globals);
    script->global_names = (char **)stasis_allocate_zeroed(count, sizeof *script->global_names);
    if (script->globals == NULL || script->global_names == NULL) {
        return false;
    }
    script->snapshot_rules.global_count = count;
    size_t kept = 0;
    for (uint32_t i = 0; i < module->global_count; i++) {
        if (!module->globals[i].is_mutable) {
            continue;
        }
        const uint32_t index = module->global_import_count + i;
        script->globals[kept].index = index;
        script->globals[kept].type = module->globals[i].type;
        script->global_names[kept] = copy_name(global_export_name(module, index));
        if (script->global_names[kept] == NULL) {
            return false;
        }
        kept++;
    }

    const struct module_memory *memory = &module->memories[0];
    uint32_t maximum = STASIS_MAX_MEMORY_PAGES;
    if (memory->has_maximum && memory->maximum < maximum) {
        maximum = memory->maximum;
    }
    script->snapshot_rules.identity = script->identity;
    script->snapshot_rules.minimum_pages = memory->minimum;
    script->snapshot_rules.maximum_pages = maximum;
    script->snapshot_rules.globals = script->globals;
    script->snapshot_rules.interface_data = load->interface_data;
    return true;
}

/* Fills in what a script that has passed the load check keeps of the check: its identity, the
 * export name of each entry point it provides, and of its cabi_post_*, why it isn't snapshottable,
 * or else what its snapshots must agree with, and where its code is, which the reader maps only if
 * WAMR's trap reports need it. Returns false if there's no memory for it. */
static bool keep_check(stasis_script *script, struct load *load) {
    memcpy(script->identity, load->module.identity, sizeof script->identity);
    script->entry_point_count = load->kind->entry_point_count;
    script->entry_points =
        (char **)stasis_allocate_zeroed(script->entry_point_count, sizeof *script->entry_points);
    script->post_returns =
        (char **)stasis_allocate_zeroed(script->entry_point_count, sizeof *script->post_returns);
    if (script->entry_points == NULL || script->post_returns == NULL) {
        return false;
    }
    for (size_t id = 0; id < script->entry_point_count; id++) {
        if (load->entry_points[id].count != 1) {
            continue;
        }
        const stasis_string name = load->entry_points[id].first->name;
        script->entry_points[id] = copy_name(name);
        if (script->entry_points[id] == NULL) {
            return false;
        }
        const struct module_export *post_return = find_post_return(load, name);
        if (post_return != NULL) {
            script->post_returns[id] = copy_name(post_return->name);
            if (script->post_returns[id] == NULL) {
                return false;
            }
        }
    }
    for (uint32_t i = 0; i < load->module.export_count; i++) {
        if (stasis_string_is(load->module.exports[i].name, STASIS_INITIALIZE_NAME)) {
            script->initializes = true;
        }
    }
    /* The reason is measured first, and then written. */
    struct message reason;
    stasis_message_init(&reason, NULL, 0);
    stasis_explain_unsnapshottable(&load->module, &reason);
    if (reason.lines > 0) {
        const size_t size = reason.total + 1;
        script->unsnapshottable = stasis_allocate(size);
        if (script->unsnapshottable == NULL) {
            return false;
        }
        stasis_message_init(&reason, script->unsnapshottable, size);
        stasis_explain_unsnapshottable(&load->module, &reason);
    } else if (!keep_snapshot_rules(script, load)) {
        return false;
    }
    script->code = load->module.code;
    memset(&load->module.code, 0, sizeof load->module.code);
    return true;
}

/* Lays out each of the interface's entry points for a script that has passed the load check, and
 * so has its entry point count. Returns as stasis_function_layout_init does. */
static stasis_status lay_out_entry_points(stasis_script *script, struct load *load) {
    script->entry_point_layouts =
        stasis_allocate_zeroed(script->entry_point_count, sizeof *script->entry_point_layouts);
    if (script->entry_point_layouts == NULL) {
        return STASIS_OUT_OF_MEMORY;
    }
    for (size_t id = 0; id < script->entry_point_count; id++) {
        const stasis_entry_point *entry_point = &load->kind->entry_points[id];
        const stasis_status status = stasis_function_layout_init(
            &script->entry_point_layouts[id], &load->layouts, entry_point->parameters,
            entry_point->parameter_count, entry_point->result);
        if (status != STASIS_OK) {
            return status;
        }
    }
    return STASIS_OK;
}

/* Loads the module into a script of its own, once the arguments have been checked. */
static stasis_status load_script(stasis_library *library, struct load *load, const uint8_t *bytes,
                                 size_t size, stasis_script **script) {
    const enum module_read read =
        stasis_module_read(&load->module, bytes, size, stasis_wamr_frames_have_offsets());
    if (read == MODULE_READ_OUT_OF_MEMORY) {
        return STASIS_OUT_OF_MEMORY;
    }
    if (size > UINT32_MAX) {
        problem(load, STASIS_INVALID_MODULE, "the module is larger than 4 GiB");
        return load->status;
    }

    stasis_script *created = stasis_allocate_zeroed(1, sizeof *created);
    if (created == NULL) {
        return STASIS_OUT_OF_MEMORY;
    }
    created->bytes = stasis_allocate(size);
    if (created->bytes == NULL) {
        free_script(created);
        return STASIS_OUT_OF_MEMORY;
    }
    if (size > 0) {
        memcpy(created->bytes, bytes, size);
    }
    if (!stasis_walk_init(&load->walk, load->interface_data) ||
        !stasis_layout_builder_init(&load->layouts, &load->walk)) {
        free_script(created);
        return STASIS_OUT_OF_MEMORY;
    }
    const stasis_status prepared =
        stasis_host_functions_init(&created->host_functions, &load->layouts);
    if (prepared != STASIS_OK) {
        free_script(created);
        return prepared;
    }

    /* WAMR links the module's imports as it loads it, to the host functions registered then. */
    char wamr_message[WAMR_MESSAGE_SIZE] = "";
    stasis_lock();
    const bool registered = stasis_host_functions_register(&created->host_functions);
    if (registered) {
        created->module =
            wasm_runtime_load(created->bytes, (uint32_t)size, wamr_message, sizeof wamr_message);
        stasis_host_functions_unregister(&created->host_functions);
    }
    stasis_unlock();
    if (!registered) {
        free_script(created);
        return STASIS_OUT_OF_MEMORY;
    }
    if (created->module == NULL) {
        free_script(created);
        problem(load, STASIS_INVALID_MODULE, "%s", wamr_message);
        return load->status;
    }

    stasis_status status = STASIS_OK;
    if (read == MODULE_READ_MALFORMED) {
        problem(load, STASIS_INVALID_MODULE,
                "the module is valid WebAssembly, but in a form Stasis can't read");
        status = load->status;
    } else {
        status = check(load);
    }
    if (status == STASIS_OK && !keep_check(created, load)) {
        status = STASIS_OUT_OF_MEMORY;
    }
    if (status == STASIS_OK) {
        status = lay_out_entry_points(created, load);
    }

    stasis_lock();
    if (status == STASIS_OK) {
        created->library = library;
        created->interface_data = load->interface_data;
        library->script_count++;
    } else {
        wasm_runtime_unload(created->module);
    }
    stasis_unlock();
    if (status != STASIS_OK) {
        free_script(created);
        return status;
    }
    *script = created;
    return STASIS_OK;
}

stasis_status stasis_script_load(stasis_library *library, const stasis_interface *interface_data,
                                 const void *bytes, size_t size, char *message, size_t message_size,
                                 stasis_script **script) {
    if (script != NULL) {
        *script = NULL;
    }
    if (message == NULL && message_size > 0) {
        return STASIS_INVALID_ARGUMENT;
    }
    struct message written;
    stasis_message_init(&written, message, message_size);
    if (library == NULL || interface_data == NULL || script == NULL ||
        (bytes == NULL && size > 0)) {
        return STASIS_INVALID_ARGUMENT;
    }
    if (!stasis_interface_usable(interface_data)) {
        return STASIS_INVALID_ARGUMENT;
    }

    struct load load;
    memset(&load, 0, sizeof load);
    load.interface_data = interface_data;
    load.kind = &interface_data->scripts[0];
    load.message = &written;
    load.status = STASIS_OK;
    const stasis_status status = load_script(library, &load, bytes, size, script);
    free_load(&load);
    if (status != STASIS_OK && status != load.status) {
        /* The failure wasn't one the message names, so it says nothing. */
        stasis_message_init(&written, message, message_size);
    }
    return status;
}

stasis_status stasis_script_destroy(stasis_script *script) {
    if (script == NULL) {
        return STASIS_OK;
    }
    stasis_lock();
    if (script->instance_count > 0) {
        stasis_unlock();
        return STASIS_REFUSED_IN_USE;
    }
    wasm_runtime_unload(script->module);
    script->library->script_count--;
    stasis_unlock();
    free_script(script);
    return STASIS_OK;
}

const uint8_t *stasis_script_identity(const stasis_script *script) {
    return script == NULL ? NULL : script->identity;
}

bool stasis_script_snapshottable(const stasis_script *script, const char **reason) {
    if (reason != NULL) {
        *reason = script == NULL ? NULL : script->unsnapshottable;
    }
    if (script == NULL) {
        return false;
    }
    return script->unsnapshottable == NULL;
}

bool stasis_script_provides(const stasis_script *script, uint32_t entry_point) {
    if (script == NULL || entry_point >= script->entry_point_count) {
        return false;
    }
    return script->entry_points[entry_point] != NULL;
}
