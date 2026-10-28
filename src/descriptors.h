/* What the core derives from an interface's descriptors: the hash of each entry point's signature,
 * and the core WebAssembly types each entry point's values flatten to, as the canonical ABI
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
 * that stasis_interface_script_kind finds, the interface's names, and its entry points' names and
 * parameters. */
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

/* The core function type that an entry point's export has, as the canonical ABI flattens it to be
 * lifted: its parameters, or one pointer to them past STASIS_MAX_FLAT_PARAMETERS, and its result,
 * or one pointer to it past STASIS_MAX_FLAT_RESULTS. Each type is a module.h value type. */
struct core_type {
    uint8_t parameters[STASIS_MAX_FLAT_PARAMETERS];
    uint32_t parameter_count;
    uint8_t results[STASIS_MAX_FLAT_RESULTS];
    uint32_t result_count;
};

/* Writes the core function type of an entry point's export. Returns false if its descriptors are
 * malformed. */
bool stasis_entry_point_core_type(struct descriptor_walk *walk,
                                  const stasis_entry_point *entry_point, struct core_type *type);

/* The number of bytes that a value of `value`'s type takes in a C struct, or 0 if its descriptor
 * is malformed. */
size_t stasis_value_size(const stasis_interface *interface_data, const stasis_value *value);

#endif
