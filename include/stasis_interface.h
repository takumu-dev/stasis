/**
 * @file stasis_interface.h
 * @brief An interface's descriptors: everything Stasis knows of an interface.
 *
 * `stasis-gen host` writes an interface's descriptors into the host's side of it, as one constant
 * ::stasis_interface that a host passes to Stasis with each script it loads. From the descriptors,
 * Stasis derives the name of every import and export of a script, and lifts, lowers and checks
 * every value that crosses between the host and a script. The data holds no import or export
 * names of its own.
 *
 * Values cross in C structs. An entry point's typed caller puts its arguments in a struct for
 * Stasis, and Stasis puts a host function's arguments in a struct for the host function's thunk,
 * which calls the host's own typed function. Records are structs of their own. A descriptor gives
 * each value's type and its offset in its struct.
 *
 * A host needs this header only to read the descriptors, or to build its own. In this release, the
 * types are promised only as the generator writes them.
 *
 * This header is C99 that also compiles as C++11 or later, and it may include nothing beyond
 * `<stddef.h>`, `<stdint.h>`, `<stdbool.h>` and stasis.h.
 */

#ifndef STASIS_INTERFACE_H
#define STASIS_INTERFACE_H

#include "stasis.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The descriptor format that this header declares.
 *
 * A ::stasis_interface names its format first. The format only grows, and Stasis reads every
 * format up to its own.
 */
#define STASIS_INTERFACE_FORMAT 1

/**
 * @brief The type of a value that crosses between the host and a script.
 *
 * Each type crosses as one C type, named here with the interface's own name for the type.
 */
typedef enum stasis_type {
    /** `uint8_t`, for `u8`. */
    STASIS_TYPE_U8 = 0,
    /** `uint16_t`, for `u16`. */
    STASIS_TYPE_U16 = 1,
    /** `uint32_t`, for `u32`. */
    STASIS_TYPE_U32 = 2,
    /** `uint64_t`, for `u64`. */
    STASIS_TYPE_U64 = 3,
    /** `int8_t`, for `s8`. */
    STASIS_TYPE_S8 = 4,
    /** `int16_t`, for `s16`. */
    STASIS_TYPE_S16 = 5,
    /** `int32_t`, for `s32`. */
    STASIS_TYPE_S32 = 6,
    /** `int64_t`, for `s64`. */
    STASIS_TYPE_S64 = 7,
    /** `float`, for `f32`. */
    STASIS_TYPE_F32 = 8,
    /** `double`, for `f64`. */
    STASIS_TYPE_F64 = 9,
    /** `bool`, for `bool`. */
    STASIS_TYPE_BOOL = 10,
    /** ::stasis_string, for `string`. */
    STASIS_TYPE_STRING = 11,
    /** A record, as a struct of its own that a ::stasis_record describes. */
    STASIS_TYPE_RECORD = 12,
    /** A handle, as a pointer to its kind of host object's opaque type. */
    STASIS_TYPE_HANDLE = 13,
    /**
     * Not a type. It makes the type 32 bits wide, so that a host's compiler lays descriptors out
     * as Stasis's does.
     */
    STASIS_TYPE_MAX_ENUM = 0x7FFFFFFF,
} stasis_type;

/**
 * @brief A value in a C struct: a parameter, a record's field, or a result.
 */
typedef struct stasis_value {
    /** The value's type. */
    stasis_type type;
    /**
     * For ::STASIS_TYPE_RECORD, the record's index in stasis_interface::records. For
     * ::STASIS_TYPE_HANDLE, the id of the handle's kind of host object, which is its index in
     * stasis_interface::kinds. 0 for every other type.
     */
    uint32_t index;
    /**
     * The value's offset in the struct that holds it, from `offsetof`. A result is the whole of
     * the storage it goes in, so its offset is 0.
     */
    size_t offset;
} stasis_value;

/**
 * @brief One of a record's fields.
 */
typedef struct stasis_field {
    /** The field's name in the interface, such as `"weapon"`. */
    const char *name;
    /** The field's type, and its offset in the record's struct. */
    stasis_value value;
} stasis_field;

/**
 * @brief A record, which crosses as a struct of its own.
 */
typedef struct stasis_record {
    /** The record's name in the interface, such as `"hit"`. */
    const char *name;
    /** The index in stasis_interface::groups of the group that declares the record. */
    uint32_t group;
    /** The record's fields, in order. */
    const stasis_field *fields;
    /** The number of fields, which is at least 1. */
    size_t field_count;
    /** The size of the record's struct, from `sizeof`. */
    size_t size;
} stasis_record;

/**
 * @brief A thunk: the function through which Stasis calls one host function.
 *
 * A thunk takes the host function's arguments from their struct, calls the host's own typed
 * function with them, and stores its result.
 *
 * @param instance The script instance whose call reached the host function.
 * @param arguments The host function's arguments, in a struct laid out as its descriptor says, or
 * null when it takes none.
 * @param result Where the host function's result goes, or null when it returns nothing.
 */
typedef void stasis_thunk(stasis_instance *instance, const void *arguments, void *result);

/**
 * @brief A host function, or a method of a kind of host object.
 */
typedef struct stasis_host_function {
    /** The function's name in the interface, such as `"find"`, or a method's, such as `"name"`. */
    const char *name;
    /**
     * The function's parameters, in order, each with its offset in the argument struct. A
     * method's first parameter is the handle of the host object it's called on.
     */
    const stasis_value *parameters;
    /** The number of parameters. */
    size_t parameter_count;
    /** The size of the argument struct, from `sizeof`, or 0 when the function takes none. */
    size_t arguments_size;
    /** The function's result, or null when it returns nothing. */
    const stasis_value *result;
    /** The function's thunk. */
    stasis_thunk *thunk;
} stasis_host_function;

/**
 * @brief A kind of host object, with its methods.
 */
typedef struct stasis_object_kind {
    /** The kind's name in the interface, such as `"entity"`. */
    const char *name;
    /**
     * The kind's qualified name, as snapshots record it: the name of the group that declares it,
     * a dot and its own name, such as `"world.entity"`.
     */
    const char *qualified_name;
    /** The index in stasis_interface::groups of the group that declares the kind. */
    uint32_t group;
    /** The kind's methods, in order. */
    const stasis_host_function *methods;
    /** The number of methods. */
    size_t method_count;
} stasis_object_kind;

/**
 * @brief A group, with its host functions.
 *
 * The group's kinds of host object and records name it by its index.
 */
typedef struct stasis_group {
    /** The group's name in the interface, such as `"world"`. */
    const char *name;
    /** The group's host functions, in order, without its kinds of host object's methods. */
    const stasis_host_function *functions;
    /** The number of host functions. */
    size_t function_count;
} stasis_group;

/**
 * @brief An entry point.
 */
typedef struct stasis_entry_point {
    /** The entry point's name in the interface, such as `"tick"`. */
    const char *name;
    /** Whether the entry point is optional, so that a script may leave it out. */
    bool optional;
    /**
     * The entry point's parameters, in order, each with its offset in the struct that its typed
     * caller fills.
     */
    const stasis_value *parameters;
    /** The number of parameters. */
    size_t parameter_count;
    /** The entry point's result, or null when it returns nothing. */
    const stasis_value *result;
} stasis_entry_point;

/**
 * @brief A kind of script, with its entry points.
 */
typedef struct stasis_script_kind {
    /** The kind's name in the interface, such as `"npc"`. */
    const char *name;
    /** The kind's entry points, in order. An entry point's id is its index here. */
    const stasis_entry_point *entry_points;
    /** The number of entry points. */
    size_t entry_point_count;
} stasis_script_kind;

/**
 * @brief An interface's descriptors, in the format that ::STASIS_INTERFACE_FORMAT names.
 *
 * Every list holds its items in the order the interface file declares them.
 */
struct stasis_interface {
    /** The descriptor format of the data, which comes first. */
    uint32_t format;
    /** The interface's namespace, such as `"acme"`. */
    const char *space;
    /** The interface's name, such as `"dungeon"`. */
    const char *name;
    /** The interface version's major number. */
    uint32_t major;
    /** The interface version's minor number. */
    uint32_t minor;
    /** The interface version's patch number. */
    uint32_t patch;
    /** The interface's groups. */
    const stasis_group *groups;
    /** The number of groups. */
    size_t group_count;
    /** Every group's kinds of host object. A kind's id is its index here. */
    const stasis_object_kind *kinds;
    /** The number of kinds of host object. */
    size_t kind_count;
    /** Every group's records. */
    const stasis_record *records;
    /** The number of records. */
    size_t record_count;
    /** The interface's kinds of script. */
    const stasis_script_kind *scripts;
    /** The number of kinds of script, which is 1. */
    size_t script_count;
};

#ifdef __cplusplus
}
#endif

#endif
