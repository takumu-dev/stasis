#include "layout.h"

#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "boundary.h"
#include "descriptors.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Where laying out is within one record: the record's offset in the C struct that holds the
 * values, its alignment in memory, and the name of the field the walk has come to, which is null
 * before the first. */
struct layout_frame {
    size_t offset;
    size_t alignment;
    const char *field;
};

/* Rounds `size` up to a multiple of `alignment`. */
static uint64_t align_up(uint64_t size, size_t alignment) {
    return (size + alignment - 1) / alignment * alignment;
}

/* Takes the strictest alignment in memory of the values a walk comes to. */
static void find_alignment(void *context, enum walk_event event, const stasis_value *value,
                           const char *name) {
    (void)name;
    size_t *alignment = context;
    if (event == WALK_LEAF && stasis_memory_alignment(value) > *alignment) {
        *alignment = stasis_memory_alignment(value);
    }
}

bool stasis_layout_builder_init(struct layout_builder *builder, struct descriptor_walk *walk) {
    const size_t record_count = walk->interface_data->record_count;
    builder->walk = walk;
    builder->alignments = NULL;
    builder->frames = NULL;
    if (record_count == 0) {
        return true;
    }
    builder->alignments = stasis_allocate_zeroed(record_count, sizeof *builder->alignments);
    /* Records that don't hold themselves nest no deeper than there are records. */
    builder->frames = stasis_allocate_zeroed(record_count, sizeof *builder->frames);
    if (builder->alignments == NULL || builder->frames == NULL) {
        return false;
    }
    /* A record is as aligned as the strictest value it holds. One whose descriptors are malformed
     * keeps an alignment of 1, which no layout uses, since walking a value that holds it fails. */
    for (size_t i = 0; i < record_count; i++) {
        const stasis_value record = {STASIS_TYPE_RECORD, (uint32_t)i, 0};
        builder->alignments[i] = 1;
        (void)stasis_walk_value(walk, &record, find_alignment, &builder->alignments[i]);
    }
    return true;
}

void stasis_layout_builder_free(struct layout_builder *builder) {
    stasis_deallocate(builder->alignments);
    stasis_deallocate(builder->frames);
    builder->alignments = NULL;
    builder->frames = NULL;
}

/* Values being laid out: measured first, with the layout's leaves null, and then written into the
 * room measured. */
struct laying_out {
    const struct layout_builder *builder;
    struct layout *layout;
    /* The parameter being walked, from 1, or 0 for a result. */
    size_t position;
    /* The number of records the walk is within. */
    size_t depth;
    /* How far the tuple reaches in memory so far, which is kept apart from the layout's size so
     * that it can't wrap around before it's checked. */
    uint64_t memory_size;
    /* The room for leaves and paths that measuring found, and the bytes of paths so far. */
    size_t leaf_room;
    size_t paths_room;
    size_t paths_size;
};

/* The offset of `value`, which the walk has come to, in the C struct that holds the values: its
 * own, within the record that holds it, if any. */
static size_t struct_offset(const struct laying_out *laying, const stasis_value *value) {
    if (laying->depth == 0) {
        return value->offset;
    }
    return laying->builder->frames[laying->depth - 1].offset + value->offset;
}

/* The number of bytes in the path of a leaf the walk has come to, its NUL included, which is 0
 * when the leaf is in no record. */
static size_t path_size(const struct laying_out *laying) {
    size_t size = 0;
    for (size_t i = 0; i < laying->depth; i++) {
        size += strlen(laying->builder->frames[i].field) + 1;
    }
    return size;
}

/* Writes the path of a leaf the walk has come to at `path`. */
static void write_path(const struct laying_out *laying, char *path) {
    size_t length = 0;
    for (size_t i = 0; i < laying->depth; i++) {
        const char *field = laying->builder->frames[i].field;
        if (i > 0) {
            path[length++] = '.';
        }
        memcpy(path + length, field, strlen(field));
        length += strlen(field);
    }
    path[length] = '\0';
}

/* Adds a leaf, after those before it among the core values and in memory. */
static void add_leaf(struct laying_out *laying, const stasis_value *value) {
    struct layout *layout = laying->layout;
    const size_t alignment = stasis_memory_alignment(value);
    const uint64_t memory_offset = align_up(laying->memory_size, alignment);
    laying->memory_size = memory_offset + stasis_memory_size(value);
    if (alignment > layout->memory_alignment) {
        layout->memory_alignment = alignment;
    }
    if (value->type == STASIS_TYPE_HANDLE) {
        layout->holds_handle = true;
    }
    const size_t size = path_size(laying);
    /* Writing walks as measuring did, and the room it found is checked all the same. */
    if (layout->leaves != NULL && layout->leaf_count < laying->leaf_room &&
        laying->paths_size <= laying->paths_room &&
        size <= laying->paths_room - laying->paths_size) {
        struct leaf *leaf = &layout->leaves[layout->leaf_count];
        leaf->value = *value;
        leaf->value.offset = struct_offset(laying, value);
        leaf->position = laying->position;
        leaf->path = NULL;
        leaf->slot = layout->flat_count;
        leaf->memory_offset = (size_t)memory_offset;
        if (size > 0) {
            char *path = layout->paths + laying->paths_size;
            write_path(laying, path);
            leaf->path = path;
        }
    }
    layout->leaf_count++;
    layout->flat_count += value->type == STASIS_TYPE_STRING ? 2 : 1;
    laying->paths_size += size;
}

/* Lays out each event of a walk: a record's start, which is as aligned in memory as the record, and
 * its end, which pads it to a multiple of that, each field's name, and each leaf. */
static void lay_out(void *context, enum walk_event event, const stasis_value *value,
                    const char *name) {
    struct laying_out *laying = context;
    struct layout_frame *frames = laying->builder->frames;
    switch (event) {
    case WALK_RECORD: {
        struct layout_frame *frame = &frames[laying->depth];
        frame->offset = struct_offset(laying, value);
        frame->alignment = laying->builder->alignments[value->index];
        frame->field = NULL;
        laying->depth++;
        laying->memory_size = align_up(laying->memory_size, frame->alignment);
        break;
    }
    case WALK_FIELD:
        frames[laying->depth - 1].field = name;
        break;
    case WALK_LEAF:
        add_leaf(laying, value);
        break;
    case WALK_END:
        laying->depth--;
        laying->memory_size = align_up(laying->memory_size, frames[laying->depth].alignment);
        break;
    }
}

/* Walks the `count` values at `values` into the layout, as parameters, or as a result when
 * `parameters` is false. Returns false if their descriptors are malformed, or the tuple of them
 * takes 4 GiB or more. */
static bool walk_values(struct laying_out *laying, const stasis_value *values, size_t count,
                        bool parameters) {
    struct layout *layout = laying->layout;
    layout->leaf_count = 0;
    layout->flat_count = 0;
    layout->memory_alignment = 1;
    layout->holds_handle = false;
    laying->memory_size = 0;
    laying->paths_size = 0;
    for (size_t i = 0; i < count; i++) {
        laying->position = parameters ? i + 1 : 0;
        laying->depth = 0;
        if (!stasis_walk_value(laying->builder->walk, &values[i], lay_out, laying)) {
            return false;
        }
    }
    laying->memory_size = align_up(laying->memory_size, layout->memory_alignment);
    if (laying->memory_size > UINT32_MAX) {
        return false;
    }
    layout->memory_size = (size_t)laying->memory_size;
    return true;
}

/* Frees the leaves and paths of a layout. */
static void free_layout(struct layout *layout) {
    stasis_deallocate(layout->leaves);
    stasis_deallocate(layout->paths);
    memset(layout, 0, sizeof *layout);
}

/* Lays out the `count` values at `values`, as parameters, or as a result when `parameters` is
 * false, measuring them first. Returns as stasis_function_layout_init does. */
static stasis_status lay_out_values(struct layout_builder *builder, const stasis_value *values,
                                    size_t count, bool parameters, struct layout *layout) {
    memset(layout, 0, sizeof *layout);
    struct laying_out laying;
    memset(&laying, 0, sizeof laying);
    laying.builder = builder;
    laying.layout = layout;
    if (!walk_values(&laying, values, count, parameters)) {
        return STASIS_INVALID_ARGUMENT;
    }
    if (layout->leaf_count == 0) {
        return STASIS_OK;
    }
    laying.leaf_room = layout->leaf_count;
    laying.paths_room = laying.paths_size;
    layout->leaves = stasis_allocate_zeroed(laying.leaf_room, sizeof *layout->leaves);
    if (laying.paths_room > 0) {
        layout->paths = stasis_allocate(laying.paths_room);
    }
    if (layout->leaves == NULL || (laying.paths_room > 0 && layout->paths == NULL)) {
        free_layout(layout);
        return STASIS_OUT_OF_MEMORY;
    }
    /* The same values walk the same way again. */
    (void)walk_values(&laying, values, count, parameters);
    return STASIS_OK;
}

stasis_status stasis_function_layout_init(struct function_layout *layout,
                                          struct layout_builder *builder,
                                          const stasis_value *parameters, size_t parameter_count,
                                          const stasis_value *result) {
    memset(layout, 0, sizeof *layout);
    stasis_status status =
        lay_out_values(builder, parameters, parameter_count, true, &layout->parameters);
    if (status == STASIS_OK) {
        status = lay_out_values(builder, result, result != NULL ? 1 : 0, false, &layout->result);
    }
    if (status != STASIS_OK) {
        stasis_function_layout_free(layout);
    }
    return status;
}

void stasis_function_layout_free(struct function_layout *layout) {
    free_layout(&layout->parameters);
    free_layout(&layout->result);
}

bool stasis_function_crosses(const struct function_layout *layout) {
    if (layout->parameters.holds_handle || layout->result.holds_handle) {
        return false;
    }
    return true;
}
