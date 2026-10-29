#include "handles.h"

#include "allocator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The most of its slots that a hash table fills: 3 in 4, so that a lookup soon comes to an empty
 * slot. Each table probes from the slot a handle hashes to, one slot at a time. */
enum {
    LOAD_NUMERATOR = 3,
    LOAD_DENOMINATOR = 4,
    SMALLEST_CAPACITY = 8,
};

/* Mixes the bits of `key`, as MurmurHash3's 64-bit finaliser does, so that keys that differ in few
 * bits, as handles' values and host objects' addresses do, hash to slots far apart. */
static uint64_t mix(uint64_t key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdU;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53U;
    key ^= key >> 33;
    return key;
}

/* The slot that a handle hashes to in the table by value, or by host object, among `mask` + 1. A
 * host object hashes by the bits of its address. */
static size_t slot_of_value(uint32_t value, size_t mask) { return (size_t)mix(value) & mask; }

_Static_assert(sizeof(void *) <= sizeof(uint64_t), "An address fits in 64 bits.");

static size_t slot_of_object(uint32_t kind, const void *object, size_t mask) {
    uint64_t address = 0;
    memcpy(&address, (const void *)&object, sizeof object);
    return (size_t)(mix(address) ^ mix(kind)) & mask;
}

/* The slot that `handle` hashes to in the table by host object if `by_object`, or by value. */
static size_t home_of(const struct handle *handle, bool by_object, size_t mask) {
    return by_object ? slot_of_object(handle->kind, handle->object, mask)
                     : slot_of_value(handle->value, mask);
}

/* Puts `handle` into the first empty slot from the one it hashes to. There's always one, since a
 * table is never full. */
static void put(struct handle *slots, size_t mask, const struct handle *handle, bool by_object) {
    size_t slot = home_of(handle, by_object, mask);
    while (slots[slot].value != 0) {
        slot = (slot + 1) & mask;
    }
    slots[slot] = *handle;
}

/* Empties the slot `hole`, and moves each handle after it, up to the next empty slot, back into the
 * hole when the handle hashes to a slot no later than the hole, so that a lookup that probes from
 * where it hashes still finds it. */
static void take_out(struct handle *slots, size_t mask, size_t hole, bool by_object) {
    for (size_t slot = (hole + 1) & mask; slots[slot].value != 0; slot = (slot + 1) & mask) {
        const size_t home = home_of(&slots[slot], by_object, mask);
        if (((slot - home) & mask) >= ((slot - hole) & mask)) {
            slots[hole] = slots[slot];
            hole = slot;
        }
    }
    memset(&slots[hole], 0, sizeof slots[hole]);
}

/* The slot of the live handle with the value `value` in the table by value, or of the one for
 * `object` of the kind `kind` in the table by host object, or SIZE_MAX if there's none. */
static size_t slot_by_value(const struct handle_table *table, uint32_t value) {
    if (table->capacity == 0 || value == 0) {
        return SIZE_MAX;
    }
    const size_t mask = table->capacity - 1;
    for (size_t slot = slot_of_value(value, mask); table->by_value[slot].value != 0;
         slot = (slot + 1) & mask) {
        if (table->by_value[slot].value == value) {
            return slot;
        }
    }
    return SIZE_MAX;
}

static size_t slot_by_object(const struct handle_table *table, uint32_t kind, const void *object) {
    if (table->capacity == 0) {
        return SIZE_MAX;
    }
    const size_t mask = table->capacity - 1;
    for (size_t slot = slot_of_object(kind, object, mask); table->by_object[slot].value != 0;
         slot = (slot + 1) & mask) {
        const struct handle *handle = &table->by_object[slot];
        if (handle->kind == kind && handle->object == object) {
            return slot;
        }
    }
    return SIZE_MAX;
}

/* Whether `count` live handles fit in `capacity` slots. */
static bool fits(size_t count, size_t capacity) {
    return count <= capacity / LOAD_DENOMINATOR * LOAD_NUMERATOR;
}

void stasis_handle_table_init(struct handle_table *table) { memset(table, 0, sizeof *table); }

void stasis_handle_table_free(struct handle_table *table) {
    stasis_deallocate(table->by_value);
    stasis_deallocate(table->by_object);
    stasis_deallocate(table->ordered);
    memset(table, 0, sizeof *table);
}

bool stasis_handle_table_reserve(struct handle_table *table, size_t extra) {
    if (extra > SIZE_MAX / sizeof(struct handle) - table->count) {
        return false;
    }
    const size_t needed = table->count + extra;
    if (extra == 0 || (table->capacity > 0 && fits(needed, table->capacity))) {
        return true;
    }
    size_t capacity = table->capacity > 0 ? table->capacity * 2 : SMALLEST_CAPACITY;
    while (!fits(needed, capacity)) {
        capacity *= 2;
    }
    struct handle *by_value = stasis_allocate_zeroed(capacity, sizeof *by_value);
    struct handle *by_object = stasis_allocate_zeroed(capacity, sizeof *by_object);
    uint32_t *ordered = stasis_allocate(capacity * sizeof *ordered);
    if (by_value == NULL || by_object == NULL || ordered == NULL) {
        stasis_deallocate(by_value);
        stasis_deallocate(by_object);
        stasis_deallocate(ordered);
        return false;
    }
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->by_value[i].value != 0) {
            put(by_value, capacity - 1, &table->by_value[i], false);
            put(by_object, capacity - 1, &table->by_value[i], true);
        }
    }
    if (table->count > 0) {
        memcpy(ordered, table->ordered, table->count * sizeof *ordered);
    }
    stasis_deallocate(table->by_value);
    stasis_deallocate(table->by_object);
    stasis_deallocate(table->ordered);
    table->by_value = by_value;
    table->by_object = by_object;
    table->ordered = ordered;
    table->capacity = capacity;
    return true;
}

enum handle_fault stasis_handle_lift(const struct handle_table *table, uint32_t value,
                                     uint32_t kind, void **object, uint32_t *other_kind) {
    if (value == 0 || value > table->issued) {
        return HANDLE_FORGED;
    }
    const size_t slot = slot_by_value(table, value);
    *object = NULL;
    if (slot == SIZE_MAX) {
        return HANDLE_CROSSES;
    }
    const struct handle *handle = &table->by_value[slot];
    if (handle->kind != kind) {
        *other_kind = handle->kind;
        return HANDLE_OF_ANOTHER_KIND;
    }
    *object = handle->object;
    return HANDLE_CROSSES;
}

uint32_t stasis_handle_of(const struct handle_table *table, uint32_t kind, const void *object) {
    const size_t slot = slot_by_object(table, kind, object);
    return slot != SIZE_MAX ? table->by_object[slot].value : 0;
}

/* Adds the live handle `value` for `object` of the kind `kind`, whose value is greater than every
 * other live handle's, into room reserved for it. */
static void add(struct handle_table *table, uint32_t value, uint32_t kind, void *object) {
    const struct handle handle = {value, kind, object};
    put(table->by_value, table->capacity - 1, &handle, false);
    put(table->by_object, table->capacity - 1, &handle, true);
    table->ordered[table->count] = value;
    table->count++;
}

bool stasis_handle_issue(struct handle_table *table, uint32_t kind, void *object, uint32_t *value) {
    if (table->issued == STASIS_HANDLE_LIMIT || !fits(table->count + 1, table->capacity)) {
        return false;
    }
    table->issued++;
    add(table, table->issued, kind, object);
    *value = table->issued;
    return true;
}

void stasis_handle_revoke(struct handle_table *table, uint32_t kind, const void *object) {
    const size_t slot = slot_by_object(table, kind, object);
    if (slot == SIZE_MAX) {
        return;
    }
    const uint32_t value = table->by_object[slot].value;
    const size_t mask = table->capacity - 1;
    take_out(table->by_value, mask, slot_by_value(table, value), false);
    take_out(table->by_object, mask, slot, true);

    /* The value is in the ordered list, which a binary search finds it in. */
    size_t low = 0;
    size_t high = table->count;
    while (table->ordered[low] != value) {
        const size_t middle = low + ((high - low) / 2);
        if (table->ordered[middle] <= value) {
            low = middle;
        } else {
            high = middle;
        }
    }
    memmove(&table->ordered[low], &table->ordered[low + 1],
            (table->count - low - 1) * sizeof *table->ordered);
    table->count--;
}

struct handle stasis_handle_at(const struct handle_table *table, size_t position) {
    return table->by_value[slot_by_value(table, table->ordered[position])];
}

bool stasis_handle_table_restore(struct handle_table *table, uint32_t issued, size_t live) {
    if (!stasis_handle_table_reserve(table, live)) {
        return false;
    }
    table->issued = issued;
    return true;
}

void stasis_handle_bind(struct handle_table *table, uint32_t value, uint32_t kind, void *object) {
    add(table, value, kind, object);
}

#ifdef STASIS_TESTING

uint32_t stasis_testing_handle_limit(void) { return STASIS_HANDLE_LIMIT; }

#endif
