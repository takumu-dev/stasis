/* How a function's values are laid out as they cross between the host and a script, as the
 * canonical ABI lays them out: flattened into core values, or in the script's memory as a tuple. A
 * record crosses as the values it holds, and lies in memory as a tuple of its fields, as aligned as
 * the strictest of them. So a layout lists the values that aren't records, its leaves, in order,
 * with where each is in the C struct that holds them on the host's side, among the core values and
 * in memory.
 *
 * Each script lays out its interface's host functions and entry points as it loads, so that a call
 * never walks their descriptors. */

#ifndef STASIS_LAYOUT_H
#define STASIS_LAYOUT_H

#include <stasis.h>
#include <stasis_interface.h>

#include "descriptors.h"

#include <stdbool.h>
#include <stddef.h>

/* A value that isn't a record, among a function's parameters or in its result. */
struct leaf {
    /* Its type, and its offset in the C struct that holds the function's arguments, or in the
     * storage of its result. */
    stasis_value value;
    /* The parameter that it is, or is in, from 1, or 0 in a result. */
    size_t position;
    /* The names of the fields it's in, outermost first, joined by dots, as in `at.x`, or null when
     * it's in no record. */
    const char *path;
    /* The index of its first core value, among those the values flatten to. */
    size_t slot;
    /* Its offset in memory from the start of the tuple of the values. */
    size_t memory_offset;
};

/* A function's parameters, or its result. */
struct layout {
    struct leaf *leaves;
    size_t leaf_count;
    /* The number of core values they flatten to. */
    size_t flat_count;
    /* The number of bytes the tuple of them takes in memory, which is less than 2^32, and its
     * alignment, which is 1 when there are none. */
    size_t memory_size;
    size_t memory_alignment;
    /* Whether a leaf is a handle. */
    bool holds_handle;
    /* The leaves' paths, one after another. */
    char *paths;
};

/* A host function's or an entry point's parameters and result, which has no leaves when there's
 * none. */
struct function_layout {
    struct layout parameters;
    struct layout result;
};

/* What laying out an interface's functions needs: its walk, the alignment in memory of each of its
 * records, by index, and room to keep where laying out is in each record it's within. */
struct layout_frame;

struct layout_builder {
    struct descriptor_walk *walk;
    size_t *alignments;
    struct layout_frame *frames;
};

/* Starts laying out the functions of the interface that `walk` walks. Returns false if there's no
 * memory for it. Whatever it returns, `builder` must be freed with stasis_layout_builder_free. */
bool stasis_layout_builder_init(struct layout_builder *builder, struct descriptor_walk *walk);

/* Frees what laying out allocated. */
void stasis_layout_builder_free(struct layout_builder *builder);

/* Lays out a host function's or an entry point's `parameter_count` parameters and its result, if
 * `result` isn't null. Returns STASIS_OK, STASIS_OUT_OF_MEMORY, or STASIS_INVALID_ARGUMENT if their
 * descriptors are malformed, or the tuple of either takes 4 GiB or more. Unless it returns
 * STASIS_OK, `layout` is left with nothing to free. */
stasis_status stasis_function_layout_init(struct function_layout *layout,
                                          struct layout_builder *builder,
                                          const stasis_value *parameters, size_t parameter_count,
                                          const stasis_value *result);

/* Frees a function's layout. */
void stasis_function_layout_free(struct function_layout *layout);

/* Whether a function's values can cross yet: whether none of them is a handle, or holds one. */
bool stasis_function_crosses(const struct function_layout *layout);

/* How a message names the field that a leaf is in, before what holds the record it's in, as in "the
 * field `at.x` of argument 1". FIELD_FORMAT prints it, with its arguments from FIELD_ARGUMENTS
 * given the leaf's path, and it prints nothing for a leaf in no record. RESULT_FIELD_ARGUMENTS does
 * the same for a leaf in a result, as in "the field `at.x` of its result as a string". */
#define FIELD_FORMAT "%s%s%s"
#define FIELD_ARGUMENTS(path)                                                                      \
    (path) != NULL ? "the field `" : "", (path) != NULL ? (path) : "", (path) != NULL ? "` of " : ""
#define RESULT_FIELD_ARGUMENTS(path)                                                               \
    (path) != NULL ? "the field `" : "", (path) != NULL ? (path) : "",                             \
        (path) != NULL ? "` of its result as " : ""

#endif
