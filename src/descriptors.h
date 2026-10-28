/* What the core derives from an interface's descriptors: the hash of each host function's and entry
 * point's signature, and the core WebAssembly types their values flatten to, as the canonical ABI
 * flattens them.
 *
 * The descriptors are the host's constant data, which the core trusts to be as the generator
 * writes them. It still checks each index before it follows it, and that records don't hold
 * themselves, so that data from another source can't send it outside its arrays. */

#ifndef STASIS_DESCRIPTORS_H
#define STASIS_DESCRIPTORS_H

#include <stasis_interface.h>

#include "signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most core parameters a function's values flatten to, past which they go through memory, and
 * the most core results, past which they go through a return area. */
enum {
    STASIS_MAX_FLAT_PARAMETERS = 16,
    STASIS_MAX_FLAT_RESULTS = 1,
};

/* The interface's one kind of script, or null unless its data is in a descriptor format this
 * release reads, and the kind's entry points are there. */
const stasis_script_kind *stasis_interface_script_kind(const stasis_interface *interface_data);

/* Whether the parts of an interface's data that every load reads are there: the kind of script
 * that stasis_interface_script_kind finds, the interface's names, its entry points' names and
 * parameters, and its groups and kinds of host object, with their host functions' and methods'
 * names, parameters and thunks. */
bool stasis_interface_usable(const stasis_interface *interface_data);

/* What walking an interface's values needs: room to keep its place in each record it's within. */
struct walk_frame;

struct descriptor_walk {
    const stasis_interface *interface_data;
    struct walk_frame *frames;
    size_t capacity;
};

/* Starts walking the values of `interface_data`. Returns false if there's no memory for it. */
bool stasis_walk_init(struct descriptor_walk *walk, const stasis_interface *interface_data);

/* Frees what walking allocated. */
void stasis_walk_free(struct descriptor_walk *walk);

/* Writes the hash of an entry point's signature. Returns false if its descriptors are malformed. */
bool stasis_entry_point_hash(struct descriptor_walk *walk, const stasis_entry_point *entry_point,
                             char hash[STASIS_SIGNATURE_HASH_LENGTH + 1]);

/* Writes the hash of a host function's or a method's signature. Returns false if its descriptors
 * are malformed. */
bool stasis_host_function_hash(struct descriptor_walk *walk, const stasis_host_function *function,
                               char hash[STASIS_SIGNATURE_HASH_LENGTH + 1]);

/* Sets `holds` to whether a value holds a string, or is one. Returns false if its descriptors are
 * malformed. */
bool stasis_holds_string(struct descriptor_walk *walk, const stasis_value *value, bool *holds);

/* A core function type, as the canonical ABI flattens a function's values: its parameters, or one
 * pointer to them past STASIS_MAX_FLAT_PARAMETERS, and its result, or past
 * STASIS_MAX_FLAT_RESULTS, one pointer to it. An entry point's export returns that pointer, and a
 * host function's import takes it as one more parameter, which is why there's room for one more.
 * Each type is a module.h value type. */
struct core_type {
    uint8_t parameters[STASIS_MAX_FLAT_PARAMETERS + 1];
    uint32_t parameter_count;
    uint8_t results[STASIS_MAX_FLAT_RESULTS];
    uint32_t result_count;
};

/* Writes the core function type of an entry point's export. Returns false if its descriptors are
 * malformed. */
bool stasis_entry_point_core_type(struct descriptor_walk *walk,
                                  const stasis_entry_point *entry_point, struct core_type *type);

/* Writes the core function type of a host function's or a method's import. Returns false if its
 * descriptors are malformed. */
bool stasis_host_function_core_type(struct descriptor_walk *walk,
                                    const stasis_host_function *function, struct core_type *type);

/* How a message names a host function or a method: by its group and its own name, and a method by
 * its kind of host object too, as in `world.find` and `world.entity.name`.
 * HOST_FUNCTION_NAME_FORMAT prints one, with its arguments from HOST_FUNCTION_NAME_ARGUMENTS. */
struct host_function_name {
    const char *group;
    /* The kind of host object and a dot after it, or two empty strings for a host function. */
    const char *object;
    const char *dot;
    const char *function;
};

#define HOST_FUNCTION_NAME_FORMAT "`%s.%s%s%s`"
#define HOST_FUNCTION_NAME_ARGUMENTS(name) (name).group, (name).object, (name).dot, (name).function

/* How a message names `function`, one of the host functions or methods of `interface_data`. */
struct host_function_name stasis_host_function_name(const stasis_interface *interface_data,
                                                    const stasis_host_function *function);

/* The number of bytes that a value of `value`'s type takes in a C struct, or 0 if its descriptor
 * is malformed. */
size_t stasis_value_size(const stasis_interface *interface_data, const stasis_value *value);

#endif
