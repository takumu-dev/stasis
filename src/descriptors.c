#include "descriptors.h"

#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "module.h"
#include "signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where a walk is within one record: the field it comes to next, and how many fields remain. */
struct walk_frame {
    const stasis_field *next;
    size_t remaining;
};

const stasis_script_kind *stasis_interface_script_kind(const stasis_interface *interface_data) {
    if (interface_data->format < 1 || interface_data->format > STASIS_INTERFACE_FORMAT) {
        return NULL;
    }
    if (interface_data->script_count != 1 || interface_data->scripts == NULL) {
        return NULL;
    }
    const stasis_script_kind *kind = &interface_data->scripts[0];
    if (kind->entry_point_count > UINT32_MAX) {
        return NULL;
    }
    if (kind->entry_point_count > 0 && kind->entry_points == NULL) {
        return NULL;
    }
    return kind;
}

/* Whether a host function's or a method's descriptor has what calling it needs: a name, its
 * parameters, and a thunk. */
static bool host_function_usable(const stasis_host_function *function) {
    if (function->name == NULL || function->thunk == NULL) {
        return false;
    }
    if (function->parameter_count == 0) {
        return true;
    }
    return function->parameters != NULL;
}

/* Whether every group, with its host functions, is there and has names. */
static bool groups_usable(const stasis_interface *interface_data) {
    if (interface_data->group_count > 0 && interface_data->groups == NULL) {
        return false;
    }
    for (size_t g = 0; g < interface_data->group_count; g++) {
        const stasis_group *group = &interface_data->groups[g];
        if (group->name == NULL || (group->function_count > 0 && group->functions == NULL)) {
            return false;
        }
        for (size_t i = 0; i < group->function_count; i++) {
            if (!host_function_usable(&group->functions[i])) {
                return false;
            }
        }
    }
    return true;
}

/* Whether every kind of host object, with its methods, is there, has names, and is in a group that
 * is there. */
static bool kinds_usable(const stasis_interface *interface_data) {
    if (interface_data->kind_count > 0 && interface_data->kinds == NULL) {
        return false;
    }
    for (size_t k = 0; k < interface_data->kind_count; k++) {
        const stasis_object_kind *kind = &interface_data->kinds[k];
        if (kind->name == NULL || kind->group >= interface_data->group_count) {
            return false;
        }
        if (kind->method_count > 0 && kind->methods == NULL) {
            return false;
        }
        for (size_t i = 0; i < kind->method_count; i++) {
            if (!host_function_usable(&kind->methods[i])) {
                return false;
            }
        }
    }
    return true;
}

bool stasis_interface_usable(const stasis_interface *interface_data) {
    const stasis_script_kind *kind = stasis_interface_script_kind(interface_data);
    if (kind == NULL || kind->name == NULL) {
        return false;
    }
    if (interface_data->space == NULL || interface_data->name == NULL) {
        return false;
    }
    for (size_t i = 0; i < kind->entry_point_count; i++) {
        const stasis_entry_point *entry_point = &kind->entry_points[i];
        if (entry_point->name == NULL) {
            return false;
        }
        if (entry_point->parameter_count > 0 && entry_point->parameters == NULL) {
            return false;
        }
    }
    if (!groups_usable(interface_data)) {
        return false;
    }
    return kinds_usable(interface_data);
}

bool stasis_walk_init(struct descriptor_walk *walk, const stasis_interface *interface_data) {
    walk->interface_data = interface_data;
    /* Records that don't hold themselves nest no deeper than there are records. */
    walk->capacity = interface_data->record_count;
    walk->frames = NULL;
    if (walk->capacity > 0) {
        walk->frames = stasis_allocate_zeroed(walk->capacity, sizeof *walk->frames);
        if (walk->frames == NULL) {
            return false;
        }
    }
    return true;
}

void stasis_walk_free(struct descriptor_walk *walk) {
    stasis_deallocate(walk->frames);
    walk->frames = NULL;
}

/* The kind of host object a handle's value names, or null if there's no such kind, or it or its
 * group lacks a name. */
static const stasis_object_kind *kind_of(const stasis_interface *interface_data,
                                         const stasis_value *value) {
    if (value->index >= interface_data->kind_count || interface_data->kinds == NULL) {
        return NULL;
    }
    const stasis_object_kind *kind = &interface_data->kinds[value->index];
    if (kind->name == NULL || kind->group >= interface_data->group_count ||
        interface_data->groups == NULL || interface_data->groups[kind->group].name == NULL) {
        return NULL;
    }
    return kind;
}

/* The record a record's value names, or null if there's no such record or it has no fields. */
static const stasis_record *record_of(const stasis_interface *interface_data,
                                      const stasis_value *value) {
    if (value->index >= interface_data->record_count || interface_data->records == NULL) {
        return NULL;
    }
    const stasis_record *record = &interface_data->records[value->index];
    if (record->fields == NULL || record->field_count == 0) {
        return NULL;
    }
    return record;
}

/* Whether a value that isn't a record is one of the types this release knows, and a handle names
 * a kind of host object that's there. */
static bool leaf_usable(const stasis_interface *interface_data, const stasis_value *value) {
    switch (value->type) {
    case STASIS_TYPE_U8:
    case STASIS_TYPE_U16:
    case STASIS_TYPE_U32:
    case STASIS_TYPE_U64:
    case STASIS_TYPE_S8:
    case STASIS_TYPE_S16:
    case STASIS_TYPE_S32:
    case STASIS_TYPE_S64:
    case STASIS_TYPE_F32:
    case STASIS_TYPE_F64:
    case STASIS_TYPE_BOOL:
    case STASIS_TYPE_STRING:
        return true;
    case STASIS_TYPE_HANDLE:
        return kind_of(interface_data, value) != NULL;
    case STASIS_TYPE_RECORD:
    case STASIS_TYPE_MAX_ENUM:
        break;
    }
    return false;
}

bool stasis_walk_value(struct descriptor_walk *walk, const stasis_value *value, walk_visit *visit,
                       void *context) {
    const stasis_interface *interface_data = walk->interface_data;
    if (value->type != STASIS_TYPE_RECORD) {
        if (!leaf_usable(interface_data, value)) {
            return false;
        }
        visit(context, WALK_LEAF, value, NULL);
        return true;
    }
    const stasis_record *record = record_of(interface_data, value);
    if (record == NULL || walk->capacity == 0) {
        return false;
    }
    size_t depth = 1;
    walk->frames[0].next = record->fields;
    walk->frames[0].remaining = record->field_count;
    visit(context, WALK_RECORD, value, NULL);
    while (depth > 0) {
        struct walk_frame *frame = &walk->frames[depth - 1];
        if (frame->remaining == 0) {
            visit(context, WALK_END, NULL, NULL);
            depth--;
            continue;
        }
        const stasis_field *field = frame->next++;
        frame->remaining--;
        if (field->name == NULL) {
            return false;
        }
        visit(context, WALK_FIELD, NULL, field->name);
        if (field->value.type != STASIS_TYPE_RECORD) {
            if (!leaf_usable(interface_data, &field->value)) {
                return false;
            }
            visit(context, WALK_LEAF, &field->value, NULL);
            continue;
        }
        const stasis_record *inner = record_of(interface_data, &field->value);
        /* Going deeper than there are records means a record holds itself. */
        if (inner == NULL || depth == walk->capacity) {
            return false;
        }
        walk->frames[depth].next = inner->fields;
        walk->frames[depth].remaining = inner->field_count;
        depth++;
        visit(context, WALK_RECORD, &field->value, NULL);
    }
    return true;
}

/* The encoding's scalar for each of the descriptors' types that is one. */
static const stasis_signature_scalar signature_scalars[] = {
    [STASIS_TYPE_U8] = STASIS_SIGNATURE_U8,     [STASIS_TYPE_U16] = STASIS_SIGNATURE_U16,
    [STASIS_TYPE_U32] = STASIS_SIGNATURE_U32,   [STASIS_TYPE_U64] = STASIS_SIGNATURE_U64,
    [STASIS_TYPE_S8] = STASIS_SIGNATURE_S8,     [STASIS_TYPE_S16] = STASIS_SIGNATURE_S16,
    [STASIS_TYPE_S32] = STASIS_SIGNATURE_S32,   [STASIS_TYPE_S64] = STASIS_SIGNATURE_S64,
    [STASIS_TYPE_F32] = STASIS_SIGNATURE_F32,   [STASIS_TYPE_F64] = STASIS_SIGNATURE_F64,
    [STASIS_TYPE_BOOL] = STASIS_SIGNATURE_BOOL, [STASIS_TYPE_STRING] = STASIS_SIGNATURE_STRING,
};

/* A signature being encoded, with the interface its handles' kinds are in. */
struct encoding {
    stasis_signature signature;
    const stasis_interface *interface_data;
};

/* Encodes each event of a walk into the signature: a scalar or a handle, and a record's start,
 * fields and end. */
static void encode(void *context, enum walk_event event, const stasis_value *value,
                   const char *name) {
    struct encoding *encoding = context;
    switch (event) {
    case WALK_LEAF:
        if (value->type == STASIS_TYPE_HANDLE) {
            const stasis_object_kind *kind = kind_of(encoding->interface_data, value);
            stasis_signature_add_handle(&encoding->signature,
                                        encoding->interface_data->groups[kind->group].name,
                                        kind->name);
        } else {
            stasis_signature_add_scalar(&encoding->signature, signature_scalars[value->type]);
        }
        break;
    case WALK_RECORD:
        stasis_signature_begin_record(&encoding->signature);
        break;
    case WALK_FIELD:
        stasis_signature_begin_field(&encoding->signature, name);
        break;
    case WALK_END:
        stasis_signature_end_record(&encoding->signature);
        break;
    }
}

/* Writes the hash of a function's signature: its parameters', and its result's, if it has one.
 * Returns false if their descriptors are malformed. */
static bool function_hash(struct descriptor_walk *walk, const stasis_value *parameters,
                          size_t parameter_count, const stasis_value *result,
                          char hash[STASIS_SIGNATURE_HASH_LENGTH + 1]) {
    struct encoding encoding;
    encoding.interface_data = walk->interface_data;
    stasis_signature_init(&encoding.signature, NULL, 0);
    for (size_t i = 0; i < parameter_count; i++) {
        if (!stasis_walk_value(walk, &parameters[i], encode, &encoding)) {
            return false;
        }
    }
    if (result != NULL) {
        stasis_signature_begin_result(&encoding.signature);
        if (!stasis_walk_value(walk, result, encode, &encoding)) {
            return false;
        }
    }
    (void)stasis_signature_finish(&encoding.signature, hash);
    return true;
}

bool stasis_entry_point_hash(struct descriptor_walk *walk, const stasis_entry_point *entry_point,
                             char hash[STASIS_SIGNATURE_HASH_LENGTH + 1]) {
    return function_hash(walk, entry_point->parameters, entry_point->parameter_count,
                         entry_point->result, hash);
}

bool stasis_host_function_hash(struct descriptor_walk *walk, const stasis_host_function *function,
                               char hash[STASIS_SIGNATURE_HASH_LENGTH + 1]) {
    return function_hash(walk, function->parameters, function->parameter_count, function->result,
                         hash);
}

/* Notes whether a walk comes to a string. */
static void find_string(void *context, enum walk_event event, const stasis_value *value,
                        const char *name) {
    (void)name;
    if (event == WALK_LEAF && value->type == STASIS_TYPE_STRING) {
        *(bool *)context = true;
    }
}

bool stasis_holds_string(struct descriptor_walk *walk, const stasis_value *value, bool *holds) {
    *holds = false;
    return stasis_walk_value(walk, value, find_string, holds);
}

/* Values being flattened: the core types of as many as fit, and how many there are in all. */
struct flattening {
    uint8_t *types;
    size_t capacity;
    size_t count;
};

/* Adds one core type, which the types hold if there's room for it. */
static void add_flat(struct flattening *flattening, uint8_t type) {
    if (flattening->count < flattening->capacity) {
        flattening->types[flattening->count] = type;
    }
    flattening->count++;
}

/* Flattens each value that isn't a record into its core types: a string into a pointer and a
 * length, and a handle into its value. */
static void flatten(void *context, enum walk_event event, const stasis_value *value,
                    const char *name) {
    (void)name;
    if (event != WALK_LEAF) {
        return;
    }
    struct flattening *flattening = context;
    switch (value->type) {
    case STASIS_TYPE_U64:
    case STASIS_TYPE_S64:
        add_flat(flattening, MODULE_I64);
        break;
    case STASIS_TYPE_F32:
        add_flat(flattening, MODULE_F32);
        break;
    case STASIS_TYPE_F64:
        add_flat(flattening, MODULE_F64);
        break;
    case STASIS_TYPE_STRING:
        add_flat(flattening, MODULE_I32);
        add_flat(flattening, MODULE_I32);
        break;
    default:
        add_flat(flattening, MODULE_I32);
        break;
    }
}

bool stasis_flat_count(struct descriptor_walk *walk, const stasis_value *values, size_t value_count,
                       size_t *count) {
    struct flattening flattening = {NULL, 0, 0};
    for (size_t i = 0; i < value_count; i++) {
        if (!stasis_walk_value(walk, &values[i], flatten, &flattening)) {
            return false;
        }
    }
    *count = flattening.count;
    return true;
}

/* Flattens a function's parameters and result into `type`: its parameters, or one pointer to them
 * past STASIS_MAX_FLAT_PARAMETERS, and its result, which past STASIS_MAX_FLAT_RESULTS is a pointer
 * that the function returns when `returns_area` is set, and that it takes as one more parameter
 * otherwise. Returns false if their descriptors are malformed. */
static bool function_core_type(struct descriptor_walk *walk, const stasis_value *parameters,
                               size_t parameter_count, const stasis_value *result,
                               bool returns_area, struct core_type *type) {
    struct flattening flat_parameters;
    flat_parameters.types = type->parameters;
    flat_parameters.capacity = STASIS_MAX_FLAT_PARAMETERS;
    flat_parameters.count = 0;
    for (size_t i = 0; i < parameter_count; i++) {
        if (!stasis_walk_value(walk, &parameters[i], flatten, &flat_parameters)) {
            return false;
        }
    }
    if (flat_parameters.count > STASIS_MAX_FLAT_PARAMETERS) {
        type->parameters[0] = MODULE_I32;
        flat_parameters.count = 1;
    }
    type->parameter_count = (uint32_t)flat_parameters.count;

    type->result_count = 0;
    if (result != NULL) {
        struct flattening results;
        results.types = type->results;
        results.capacity = STASIS_MAX_FLAT_RESULTS;
        results.count = 0;
        if (!stasis_walk_value(walk, result, flatten, &results)) {
            return false;
        }
        if (results.count <= STASIS_MAX_FLAT_RESULTS) {
            type->result_count = (uint32_t)results.count;
        } else if (returns_area) {
            type->results[0] = MODULE_I32;
            type->result_count = 1;
        } else {
            type->parameters[type->parameter_count++] = MODULE_I32;
        }
    }
    return true;
}

bool stasis_entry_point_core_type(struct descriptor_walk *walk,
                                  const stasis_entry_point *entry_point, struct core_type *type) {
    return function_core_type(walk, entry_point->parameters, entry_point->parameter_count,
                              entry_point->result, true, type);
}

bool stasis_host_function_core_type(struct descriptor_walk *walk,
                                    const stasis_host_function *function, struct core_type *type) {
    return function_core_type(walk, function->parameters, function->parameter_count,
                              function->result, false, type);
}

/* Whether `function` is one of the `count` host functions or methods at `functions`. */
static bool among(const stasis_host_function *function, const stasis_host_function *functions,
                  size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (&functions[i] == function) {
            return true;
        }
    }
    return false;
}

struct host_function_name stasis_host_function_name(const stasis_interface *interface_data,
                                                    const stasis_host_function *function) {
    /* A host function the interface somehow lacks is named by itself, in no group. */
    struct host_function_name name = {"?", "", "", function->name};
    for (size_t g = 0; g < interface_data->group_count; g++) {
        const stasis_group *group = &interface_data->groups[g];
        if (among(function, group->functions, group->function_count)) {
            name.group = group->name;
            return name;
        }
    }
    for (size_t k = 0; k < interface_data->kind_count; k++) {
        const stasis_object_kind *kind = &interface_data->kinds[k];
        if (among(function, kind->methods, kind->method_count)) {
            name.group = interface_data->groups[kind->group].name;
            name.object = kind->name;
            name.dot = ".";
            return name;
        }
    }
    return name;
}

size_t stasis_value_size(const stasis_interface *interface_data, const stasis_value *value) {
    switch (value->type) {
    case STASIS_TYPE_U8:
    case STASIS_TYPE_S8:
        return sizeof(uint8_t);
    case STASIS_TYPE_U16:
    case STASIS_TYPE_S16:
        return sizeof(uint16_t);
    case STASIS_TYPE_U32:
    case STASIS_TYPE_S32:
        return sizeof(uint32_t);
    case STASIS_TYPE_U64:
    case STASIS_TYPE_S64:
        return sizeof(uint64_t);
    case STASIS_TYPE_F32:
        return sizeof(float);
    case STASIS_TYPE_F64:
        return sizeof(double);
    case STASIS_TYPE_BOOL:
        return sizeof(bool);
    case STASIS_TYPE_STRING:
        return sizeof(stasis_string);
    case STASIS_TYPE_HANDLE:
        return sizeof(void *);
    case STASIS_TYPE_RECORD:
        if (value->index < interface_data->record_count && interface_data->records != NULL) {
            return interface_data->records[value->index].size;
        }
        break;
    case STASIS_TYPE_MAX_ENUM:
        break;
    }
    return 0;
}
