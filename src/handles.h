/* An instance's handle table: the handle it has for each host object it has been given, and the
 * host object each live handle names.
 *
 * A host object is a pointer of the host's, which Stasis never dereferences, together with its
 * kind's id, so one pointer of two kinds is two host objects. Handles are issued in order from 1,
 * one for each host object, and never reused: revoking a handle forgets its host object, which
 * gets a handle of its own if it crosses again. So a value from 1 up to the last issued that isn't
 * live is a revoked handle, and any other, 0 included, is forged.
 *
 * The table keeps only live handles, in two hash tables, one by value and one by host object, so
 * its size follows the number of live handles, however many it has issued. Only reserving room
 * allocates, so issuing and revoking never fail for lack of memory. */

#ifndef STASIS_HANDLES_H
#define STASIS_HANDLES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most handles an instance can be given over its life: every 32-bit value but 0. The test-only
 * library, which builds the core with STASIS_TESTING, lowers it so that a test can reach it, and
 * the tests read it through stasis_testing_handle_limit. */
#ifdef STASIS_TESTING
#define STASIS_HANDLE_LIMIT 4U
uint32_t stasis_testing_handle_limit(void);
#else
#define STASIS_HANDLE_LIMIT UINT32_MAX
#endif

/* A live handle: its value, and the host object it names, with its kind's id. */
struct handle {
    uint32_t value;
    uint32_t kind;
    void *object;
};

struct handle_table {
    /* The number of handles issued so far, which is the value of the last. */
    uint32_t issued;
    /* The live handles, each in both of two hash tables of `capacity` slots, a power of 2 or 0:
     * one by value, and one by host object. An empty slot is all zero. */
    struct handle *by_value;
    struct handle *by_object;
    size_t capacity;
    size_t count;
};

/* Starts an empty table, which allocates nothing until it's first given room. */
void stasis_handle_table_init(struct handle_table *table);

/* Frees the table. */
void stasis_handle_table_free(struct handle_table *table);

/* Makes room for `extra` more live handles than the table has, so that issuing them allocates
 * nothing, which for none allocates nothing either. Returns false if there's no memory for it,
 * which changes nothing. */
bool stasis_handle_table_reserve(struct handle_table *table, size_t extra);

/* What keeps a handle that a script passes the host from crossing, if anything: the table never
 * issued it, or it names a host object of another kind than the value it crosses as. */
enum handle_fault {
    HANDLE_CROSSES,
    HANDLE_FORGED,
    HANDLE_OF_ANOTHER_KIND,
};

/* Lifts the handle `value`, which crosses as a handle to a host object of the kind `kind`. When it
 * crosses, sets `object` to the host object it names, which is null when it was revoked. When it
 * names a host object of another kind, sets `other_kind` to that kind. */
enum handle_fault stasis_handle_lift(const struct handle_table *table, uint32_t value,
                                     uint32_t kind, void **object, uint32_t *other_kind);

/* The value of the live handle for `object` of the kind `kind`, or 0 if the table has none. */
uint32_t stasis_handle_of(const struct handle_table *table, uint32_t kind, const void *object);

/* Issues the next handle for `object` of the kind `kind`, which has none, into room reserved for
 * it, and sets `value` to it. Returns false, having issued nothing, if the table has issued
 * STASIS_HANDLE_LIMIT handles already, or if no room was reserved, which every caller makes. */
bool stasis_handle_issue(struct handle_table *table, uint32_t kind, void *object, uint32_t *value);

/* Revokes the live handle for `object` of the kind `kind`, if the table has one. */
void stasis_handle_revoke(struct handle_table *table, uint32_t kind, const void *object);

#endif
