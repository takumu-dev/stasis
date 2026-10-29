#include "handle_hosts.h"

#include <stasis.h>

#include "harness.h"
#include "test_handles.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

test_handles_world_entity entities[HOST_OBJECT_COUNT];
test_handles_world_item items[HOST_OBJECT_COUNT];
struct seen seen;
struct replies replies;

void forget_seen(void) {
    for (size_t i = 0; i < MOST_HITS; i++) {
        free(seen.weapons[i]);
    }
    memset(&seen, 0, sizeof seen);
    memset(&replies, 0, sizeof replies);
}

/* Starts recording a call that takes the `count` host objects at `objects`, forgetting the last. */
static void see(const void *const *objects, size_t count) {
    ASSERT_TRUE(count <= MOST_SEEN);
    for (size_t i = 0; i < MOST_HITS; i++) {
        free(seen.weapons[i]);
    }
    const unsigned calls = seen.calls;
    memset(&seen, 0, sizeof seen);
    seen.calls = calls + 1;
    seen.object_count = count;
    for (size_t i = 0; i < count; i++) {
        seen.objects[i] = objects[i];
    }
}

/* Records a call that takes the `count` hits at `hits`, and their host objects. */
static void see_hits(const test_handles_combat_hit *hits, size_t count) {
    ASSERT_TRUE(count <= MOST_HITS);
    const void *objects[MOST_HITS];
    for (size_t i = 0; i < count; i++) {
        objects[i] = hits[i].by;
    }
    see(objects, count);
    seen.hit_count = count;
    for (size_t i = 0; i < count; i++) {
        seen.hits[i] = hits[i];
        seen.weapons[i] = malloc(hits[i].weapon.size + 1);
        ASSERT_TRUE(seen.weapons[i] != NULL);
        if (hits[i].weapon.size > 0) {
            memcpy(seen.weapons[i], hits[i].weapon.data, hits[i].weapon.size);
        }
        seen.weapons[i][hits[i].weapon.size] = '\0';
    }
}

/* Records a call that takes the one host object `object`. */
static void see_one(const void *object) { see(&object, 1); }

void test_handles_world_entity_poke(stasis_instance *instance, test_handles_world_entity *self) {
    (void)instance;
    see_one(self);
}

uint32_t test_handles_world_item_weight(stasis_instance *instance, test_handles_world_item *self) {
    (void)instance;
    see_one(self);
    return 0;
}

test_handles_world_entity *test_handles_world_find(stasis_instance *instance, uint32_t index) {
    (void)instance;
    ASSERT_TRUE(index < HOST_OBJECT_COUNT);
    return replies.replace ? replies.entity : &entities[index];
}

test_handles_world_item *test_handles_world_pick_up(stasis_instance *instance, uint32_t index) {
    (void)instance;
    ASSERT_TRUE(index < HOST_OBJECT_COUNT);
    return replies.replace ? replies.item : &items[index];
}

void test_handles_world_touch(stasis_instance *instance, test_handles_world_entity *target) {
    (void)instance;
    see_one(target);
}

void test_handles_world_meet(stasis_instance *instance, test_handles_world_entity *a,
                             test_handles_world_entity *b) {
    (void)instance;
    const void *objects[2] = {a, b};
    see(objects, 2);
}

void test_handles_world_kill(stasis_instance *instance, test_handles_world_entity *target) {
    see_one(target);
    ASSERT_INT_EQ(stasis_instance_revoke(instance, TEST_HANDLES_WORLD_ENTITY_KIND, target),
                  STASIS_OK);
}

test_handles_combat_hit test_handles_combat_last_hit(stasis_instance *instance,
                                                     test_handles_world_entity *of) {
    (void)instance;
    see_one(of);
    return replies.hit;
}

uint32_t test_handles_combat_alarm(stasis_instance *instance, test_handles_combat_hit about) {
    (void)instance;
    see_hits(&about, 1);
    return about.damage;
}

void test_handles_combat_take_hits(stasis_instance *instance, test_handles_combat_hit a,
                                   test_handles_combat_hit b, test_handles_combat_hit c) {
    (void)instance;
    const test_handles_combat_hit hits[MOST_HITS] = {a, b, c};
    see_hits(hits, MOST_HITS);
}
