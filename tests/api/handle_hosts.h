/* The host functions of handles.stasis, which every program that calls the handles script links,
 * and the host objects they hand out. Each host function records the host objects it's given, and
 * returns what the case has set. */

#ifndef STASIS_TESTS_API_HANDLE_HOSTS_H
#define STASIS_TESTS_API_HANDLE_HOSTS_H

#include <stasis.h>

#include "test_handles.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The kinds of host object, as the host defines them. Only an object's address crosses, so each
 * holds nothing that the tests read. */
struct test_handles_world_entity {
    int unused;
};

struct test_handles_world_item {
    int unused;
};

/* The number of host objects of each kind, and the most host objects and hits that a call to a
 * host function takes. */
enum {
    HOST_OBJECT_COUNT = 1024,
    MOST_SEEN = 3,
    MOST_HITS = 3,
};

/* The host objects that `find` and `pick_up` hand out, by index. */
extern test_handles_world_entity entities[HOST_OBJECT_COUNT];
extern test_handles_world_item items[HOST_OBJECT_COUNT];

/* What the last host function to take host objects was given. */
struct seen {
    /* The number of calls of host functions that take host objects since forget_seen. */
    unsigned calls;
    /* The host objects, in order, each as an address, which is null for a revoked handle. */
    const void *objects[MOST_SEEN];
    size_t object_count;
    /* The hits `alarm` and `take_hits` were given, with a copy of each weapon, NUL-terminated. */
    test_handles_combat_hit hits[MOST_HITS];
    char *weapons[MOST_HITS];
    size_t hit_count;
};

extern struct seen seen;

/* What `find` and `pick_up` return in place of the host object at their index, null included,
 * when `replace` is set, and the hit that `last_hit` returns. */
struct replies {
    bool replace;
    test_handles_world_entity *entity;
    test_handles_world_item *item;
    test_handles_combat_hit hit;
};

extern struct replies replies;

/* Frees the copies, forgets every call, and clears the replies. */
void forget_seen(void);

#endif
