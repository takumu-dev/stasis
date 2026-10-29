/* Handles crossing between the host and a script: each host object is one handle in an instance,
 * a number from 1 that's never reused, and a script can only pass the host the handles it was
 * given. */

#include <stasis.h>

#include "handle_hosts.h"
#include "harness.h"
#include "support.h"
#include "test_handles.h"

#include <stdint.h>
#include <string.h>

/* An instance of the script handles.wat, started, with its script and library. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

static struct running start_handles(void) {
    forget_seen();
    struct running running;
    running.library = create_library();
    running.script = load_script(running.library, &test_handles_interface, "handles");
    running.instance = start_instance(running.script);
    return running;
}

static void stop(struct running running) {
    destroy_all(running.instance, running.script, running.library);
    forget_seen();
}

/* The handle that `find` gives the instance for the entity at `index`. */
static uint32_t find(stasis_instance *instance, uint32_t index) {
    uint32_t handle = 0;
    ASSERT_INT_EQ(test_handles_npc_find(instance, index, &handle), STASIS_OK);
    return handle;
}

/* Handles are issued in order from 1, one for each host object, so a host object that crosses again
 * crosses as the handle it was given. */
TEST(issues_one_handle_for_each_host_object_in_order) {
    const struct running running = start_handles();
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    ASSERT_UINT_EQ(find(running.instance, 1), 2);
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    ASSERT_UINT_EQ(find(running.instance, 7), 3);
    ASSERT_UINT_EQ(find(running.instance, 1), 2);
    stop(running);
}

/* Each instance has handles of its own, numbered from 1, even for the host objects another has. */
TEST(each_instance_has_its_own_handles) {
    const struct running running = start_handles();
    stasis_instance *other = start_instance(running.script);
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    ASSERT_UINT_EQ(find(running.instance, 1), 2);
    ASSERT_UINT_EQ(find(other, 1), 1);
    ASSERT_UINT_EQ(find(other, 2), 2);
    ASSERT_UINT_EQ(find(running.instance, 2), 3);
    ASSERT_INT_EQ(stasis_instance_destroy(other), STASIS_OK);
    stop(running);
}

/* The handle that `pick_up` gives the instance for the item at `index`. */
static uint32_t pick_up(stasis_instance *instance, uint32_t index) {
    uint32_t handle = 0;
    ASSERT_INT_EQ(test_handles_npc_pick_up(instance, index, &handle), STASIS_OK);
    return handle;
}

/* A handle that the script passes a host function reaches it as the host object it names, as an
 * argument, and as the host object that a method is called on. */
TEST(a_handle_reaches_a_host_function_as_its_host_object) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    ASSERT_UINT_EQ(find(instance, 0), 1);
    ASSERT_UINT_EQ(find(instance, 1), 2);
    ASSERT_UINT_EQ(pick_up(instance, 3), 3);

    ASSERT_INT_EQ(test_handles_npc_touch(instance, 1), STASIS_OK);
    ASSERT_UINT_EQ(seen.calls, 1);
    ASSERT_SIZE_EQ(seen.object_count, 1);
    ASSERT_TRUE(seen.objects[0] == &entities[0]);
    ASSERT_INT_EQ(test_handles_npc_meet(instance, 2, 1), STASIS_OK);
    ASSERT_SIZE_EQ(seen.object_count, 2);
    ASSERT_TRUE(seen.objects[0] == &entities[1]);
    ASSERT_TRUE(seen.objects[1] == &entities[0]);
    ASSERT_INT_EQ(test_handles_npc_poke(instance, 2), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == &entities[1]);
    uint32_t weight = 99;
    ASSERT_INT_EQ(test_handles_npc_weigh(instance, 3, &weight), STASIS_OK);
    ASSERT_UINT_EQ(weight, 0);
    ASSERT_TRUE(seen.objects[0] == &items[3]);
    ASSERT_UINT_EQ(seen.calls, 4);
    stop(running);
}

/* Checks that `instance` has trapped as `kind`, with `message`, and destroys it. */
static void expect_trap(stasis_instance *instance, stasis_trap_kind kind, const char *message) {
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, kind);
    ASSERT_STR_EQ(trap->message, message);
    ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_OK);
}

/* A handle that the instance was never given, 0 included, is forged, and a script that passes one
 * to a host function traps as a boundary violation before the host function runs, whether it's an
 * argument, the host object a method is called on, or a record's field. */
TEST(a_forged_handle_traps) {
    const struct running running = start_handles();
    stasis_instance *instance = start_instance(running.script);
    ASSERT_UINT_EQ(find(instance, 0), 1);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 0), STASIS_TRAP);
    expect_trap(instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed argument 1 of `world.touch` as handle 0, which the instance "
                "was never given");

    instance = start_instance(running.script);
    ASSERT_UINT_EQ(find(instance, 0), 1);
    ASSERT_INT_EQ(test_handles_npc_poke(instance, 2), STASIS_TRAP);
    expect_trap(instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed argument 1 of `world.entity.poke` as handle 2, which the "
                "instance was never given");

    instance = start_instance(running.script);
    uint32_t damage = 99;
    ASSERT_INT_EQ(test_handles_npc_alarm_by(instance, 0xFFFFFFFFU, &damage), STASIS_TRAP);
    ASSERT_UINT_EQ(damage, 0);
    expect_trap(instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed the field `by` of argument 1 of `combat.alarm` as handle "
                "4294967295, which the instance was never given");
    ASSERT_UINT_EQ(seen.calls, 0);
    stop(running);
}

/* A handle names a host object of one kind, and a script that passes it where a host function takes
 * another kind traps as a boundary violation. */
TEST(a_handle_of_another_kind_traps) {
    const struct running running = start_handles();
    stasis_instance *instance = start_instance(running.script);
    ASSERT_UINT_EQ(find(instance, 0), 1);
    ASSERT_UINT_EQ(pick_up(instance, 0), 2);
    uint32_t weight = 99;
    ASSERT_INT_EQ(test_handles_npc_weigh(instance, 1, &weight), STASIS_TRAP);
    expect_trap(instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed argument 1 of `world.item.weight` as handle 1, which names a "
                "host object of another kind, `world.entity`");

    instance = start_instance(running.script);
    ASSERT_UINT_EQ(pick_up(instance, 0), 1);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 1), STASIS_TRAP);
    expect_trap(instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed argument 1 of `world.touch` as handle 1, which names a host "
                "object of another kind, `world.item`");
    ASSERT_UINT_EQ(seen.calls, 0);
    stop(running);
}

/* Revokes the instance's handle for the entity at `index`. */
static void revoke(stasis_instance *instance, size_t index) {
    ASSERT_INT_EQ(
        stasis_instance_revoke(instance, TEST_HANDLES_WORLD_ENTITY_KIND, &entities[index]),
        STASIS_OK);
}

/* A revoked handle names no host object, so it reaches host functions as null: as an argument, as
 * the host object a method is called on, and in a record. */
TEST(a_revoked_handle_reaches_host_functions_as_null) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    ASSERT_UINT_EQ(find(instance, 0), 1);
    ASSERT_UINT_EQ(find(instance, 1), 2);
    revoke(instance, 0);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 1), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == NULL);
    ASSERT_INT_EQ(test_handles_npc_meet(instance, 1, 2), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == NULL);
    ASSERT_TRUE(seen.objects[1] == &entities[1]);
    ASSERT_INT_EQ(test_handles_npc_poke(instance, 1), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == NULL);
    uint32_t damage = 99;
    ASSERT_INT_EQ(test_handles_npc_alarm_by(instance, 1, &damage), STASIS_OK);
    ASSERT_UINT_EQ(damage, 0);
    ASSERT_TRUE(seen.hits[0].by == NULL);
    ASSERT_UINT_EQ(seen.calls, 4);
    stop(running);
}

/* Handles are never reused, so a host object whose handle was revoked gets a new one if it crosses
 * again. Revoking a host object that has no handle, or whose handle is revoked, does nothing. */
TEST(a_revoked_host_object_that_crosses_again_gets_a_new_handle) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    ASSERT_UINT_EQ(find(instance, 0), 1);
    revoke(instance, 0);
    ASSERT_UINT_EQ(find(instance, 0), 2);
    revoke(instance, 5);
    revoke(instance, 0);
    revoke(instance, 0);
    ASSERT_UINT_EQ(find(instance, 1), 3);
    ASSERT_UINT_EQ(find(instance, 0), 4);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 2), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == NULL);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 4), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == &entities[0]);
    stop(running);
}

/* Revoking runs no script code, so it's allowed in every state: before the instance starts, while
 * it runs, as a host function revokes a handle in the instance that called it, which every later
 * host function then receives as null, and once it has trapped. */
TEST(revokes_in_every_state) {
    const struct running running = start_handles();
    stasis_instance *instance = NULL;
    ASSERT_INT_EQ(stasis_instance_create(running.script, NULL, &instance), STASIS_OK);
    revoke(instance, 0);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_OK);
    ASSERT_UINT_EQ(find(instance, 0), 1);
    ASSERT_UINT_EQ(find(instance, 1), 2);

    /* `kill` revokes the handle it's given, and the script then passes it to `touch`. Killing it
     * again revokes null, the host object a revoked handle reaches it as, which does nothing. */
    ASSERT_INT_EQ(test_handles_npc_kill(instance, 1), STASIS_OK);
    ASSERT_UINT_EQ(seen.calls, 2);
    ASSERT_TRUE(seen.objects[0] == NULL);
    ASSERT_INT_EQ(test_handles_npc_kill(instance, 1), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == NULL);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 2), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == &entities[1]);
    ASSERT_UINT_EQ(find(instance, 0), 3);

    ASSERT_INT_EQ(test_handles_npc_touch(instance, 0), STASIS_TRAP);
    revoke(instance, 1);
    ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_OK);
    stop(running);
}

/* A host object is an address of a kind, so one address of two kinds is two host objects, each with
 * a handle of its own, and revoking one leaves the other. A revoked handle names no host object, of
 * its kind or any other, so it reaches a host function that takes another kind as null too. */
TEST(one_address_of_two_kinds_is_two_host_objects) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    ASSERT_UINT_EQ(find(instance, 0), 1);
    replies.replace = true;
    replies.item = (test_handles_world_item *)(void *)&entities[0];
    ASSERT_UINT_EQ(pick_up(instance, 0), 2);
    ASSERT_INT_EQ(stasis_instance_revoke(instance, TEST_HANDLES_WORLD_ITEM_KIND, &entities[0]),
                  STASIS_OK);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 1), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == &entities[0]);
    uint32_t weight = 99;
    ASSERT_INT_EQ(test_handles_npc_weigh(instance, 2, &weight), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == NULL);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 2), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == NULL);
    stop(running);
}

/* Revoking with a null instance, or a kind's id that the interface lacks, is
 * STASIS_INVALID_ARGUMENT, and changes nothing. Null is never a host object, so revoking it does
 * nothing. */
TEST(refuses_bad_arguments_to_revoke) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    ASSERT_UINT_EQ(find(instance, 0), 1);
    ASSERT_INT_EQ(stasis_instance_revoke(instance, 2, &entities[0]), STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_instance_revoke(instance, UINT32_MAX, &entities[0]),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_instance_revoke(instance, TEST_HANDLES_WORLD_ENTITY_KIND, NULL),
                  STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_revoke(instance, 2, NULL), STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_instance_revoke(NULL, TEST_HANDLES_WORLD_ENTITY_KIND, &entities[0]),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 1), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == &entities[0]);
    stop(running);
}

/* Null is never a host object, so a host function that returns one traps as a host abort, with a
 * message that names the host function. */
TEST(a_host_function_returning_a_null_host_object_traps) {
    const struct running running = start_handles();
    stasis_instance *instance = start_instance(running.script);
    replies.replace = true;
    uint32_t handle = 99;
    ASSERT_INT_EQ(test_handles_npc_find(instance, 0, &handle), STASIS_TRAP);
    ASSERT_UINT_EQ(handle, 0);
    expect_trap(instance, STASIS_TRAP_KIND_HOST_ABORT,
                "the host function `world.find` returned a null host object");
    stop(running);
}

/* The handle that `greet` returns for the entity at `index`. */
static uint32_t greet(stasis_instance *instance, size_t index) {
    uint32_t handle = 0;
    ASSERT_INT_EQ(test_handles_npc_greet(instance, &entities[index], &handle), STASIS_OK);
    return handle;
}

/* A host object that the host passes an entry point crosses as its handle, which the instance is
 * given if it has none, so a host object crosses as the same handle whichever way it first
 * crossed. */
TEST(an_entry_point_takes_a_host_object_as_its_handle) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    ASSERT_UINT_EQ(greet(instance, 3), 1);
    ASSERT_UINT_EQ(greet(instance, 3), 1);
    ASSERT_UINT_EQ(find(instance, 4), 2);
    ASSERT_UINT_EQ(greet(instance, 4), 2);
    ASSERT_UINT_EQ(find(instance, 3), 1);
    ASSERT_INT_EQ(test_handles_npc_touch(instance, 1), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == &entities[3]);
    stop(running);
}

/* Null is never a host object, so an entry point's argument that's null, or holds one, is
 * STASIS_INVALID_ARGUMENT, before the script runs. */
TEST(refuses_a_null_host_object_as_an_entry_points_argument) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    uint32_t handle = 99;
    ASSERT_INT_EQ(test_handles_npc_greet(instance, NULL, &handle), STASIS_INVALID_ARGUMENT);
    ASSERT_UINT_EQ(handle, 0);
    test_handles_combat_hit blow;
    memset(&blow, 0, sizeof blow);
    uint32_t damage = 99;
    ASSERT_INT_EQ(test_handles_npc_relay_hit(instance, blow, &damage), STASIS_INVALID_ARGUMENT);
    ASSERT_UINT_EQ(damage, 0);
    ASSERT_UINT_EQ(seen.calls, 0);
    ASSERT_UINT_EQ(greet(instance, 0), 1);
    stop(running);
}

/* The host object that `choose` returns for the handle `value`. */
static test_handles_world_entity *choose(stasis_instance *instance, uint32_t value) {
    test_handles_world_entity *chosen = &entities[HOST_OBJECT_COUNT - 1];
    ASSERT_INT_EQ(test_handles_npc_choose(instance, value, &chosen), STASIS_OK);
    return chosen;
}

/* A handle that an entry point returns reaches the host as the host object it names, or as null
 * once it has been revoked. */
TEST(an_entry_point_returns_a_handle_as_its_host_object) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    ASSERT_UINT_EQ(find(instance, 2), 1);
    ASSERT_UINT_EQ(find(instance, 3), 2);
    ASSERT_TRUE(choose(instance, 2) == &entities[3]);
    ASSERT_TRUE(choose(instance, 1) == &entities[2]);
    revoke(instance, 2);
    ASSERT_TRUE(choose(instance, 1) == NULL);
    stop(running);
}

/* An entry point that returns a handle the instance was never given, or one of another kind than
 * its result, traps as a boundary violation, and its result is null. */
TEST(an_entry_point_returning_a_forged_handle_traps) {
    const struct running running = start_handles();
    stasis_instance *instance = start_instance(running.script);
    ASSERT_UINT_EQ(find(instance, 0), 1);
    test_handles_world_entity *chosen = &entities[0];
    ASSERT_INT_EQ(test_handles_npc_choose(instance, 2, &chosen), STASIS_TRAP);
    ASSERT_TRUE(chosen == NULL);
    expect_trap(instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the entry point `npc.choose` returned handle 2, which the instance was never "
                "given");

    instance = start_instance(running.script);
    ASSERT_INT_EQ(test_handles_npc_choose(instance, 0, &chosen), STASIS_TRAP);
    expect_trap(instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the entry point `npc.choose` returned handle 0, which the instance was never "
                "given");

    instance = start_instance(running.script);
    ASSERT_UINT_EQ(pick_up(instance, 0), 1);
    chosen = &entities[0];
    ASSERT_INT_EQ(test_handles_npc_choose(instance, 1, &chosen), STASIS_TRAP);
    ASSERT_TRUE(chosen == NULL);
    expect_trap(instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the entry point `npc.choose` returned handle 1, which names a host object of "
                "another kind, `world.item`");
    stop(running);
}

/* `text` as a stasis_string, without its NUL. */
static stasis_string text_of(const char *text) {
    stasis_string string;
    string.data = text;
    string.size = strlen(text);
    return string;
}

/* A hit by the entity at `by`, with the weapon, damage and place given. */
static test_handles_combat_hit hit_of(size_t by, const char *weapon, uint32_t damage, float x,
                                      float y) {
    test_handles_combat_hit hit;
    hit.by = &entities[by];
    hit.weapon = text_of(weapon);
    hit.damage = damage;
    hit.at.x = x;
    hit.at.y = y;
    return hit;
}

/* Checks that `hit` is the hit by the entity at `by` with the weapon, damage and place given. */
static void expect_hit(test_handles_combat_hit hit, size_t by, const char *weapon, uint32_t damage,
                       float x, float y) {
    ASSERT_TRUE(hit.by == &entities[by]);
    ASSERT_SIZE_EQ(hit.weapon.size, strlen(weapon));
    ASSERT_TRUE(hit.weapon.size == 0 || memcmp(hit.weapon.data, weapon, hit.weapon.size) == 0);
    ASSERT_UINT_EQ(hit.damage, damage);
    ASSERT_FLOAT_EQ(hit.at.x, x);
    ASSERT_FLOAT_EQ(hit.at.y, y);
}

/* The 4 bytes at `address` in the instance's memory, little-endian. */
static uint32_t load(stasis_instance *instance, uint32_t address) {
    uint32_t value = 0;
    ASSERT_INT_EQ(test_handles_npc_load(instance, address, &value), STASIS_OK);
    return value;
}

/* The 32 bits of a float. */
static uint32_t bits_of(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

/* The dungeon sample's `hit` crosses with its handle both ways, flattened, with its handle first,
 * and in a return area, laid out as the canonical ABI has it: the handle, the weapon's address and
 * size, the damage and the `vec2`, 4 bytes each. */
TEST(the_dungeons_hit_crosses_with_its_handle) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    test_handles_combat_hit result;
    ASSERT_INT_EQ(
        test_handles_npc_echo_hit(instance, hit_of(4, "sword", 12, 1.5F, -2.25F), &result),
        STASIS_OK);
    expect_hit(result, 4, "sword", 12, 1.5F, -2.25F);
    ASSERT_UINT_EQ(load(instance, 256), 1);

    uint32_t damage = 0;
    ASSERT_INT_EQ(test_handles_npc_relay_hit(instance, hit_of(4, "axe", 7, 0.0F, 1.0F), &damage),
                  STASIS_OK);
    ASSERT_UINT_EQ(damage, 7);
    ASSERT_SIZE_EQ(seen.hit_count, 1);
    expect_hit(seen.hits[0], 4, "axe", 7, 0.0F, 1.0F);
    ASSERT_STR_EQ(seen.weapons[0], "axe");

    replies.hit = hit_of(9, "bow", 3, 0.5F, 0.25F);
    ASSERT_INT_EQ(test_handles_npc_fetch_hit(instance, 1, &result), STASIS_OK);
    ASSERT_TRUE(seen.objects[0] == &entities[4]);
    expect_hit(result, 9, "bow", 3, 0.5F, 0.25F);
    ASSERT_UINT_EQ(load(instance, 320), 2);
    ASSERT_UINT_EQ(load(instance, 328), 3);
    ASSERT_UINT_EQ(load(instance, 332), 3);
    ASSERT_UINT_EQ(load(instance, 336), bits_of(0.5F));
    ASSERT_UINT_EQ(load(instance, 340), bits_of(0.25F));
    ASSERT_UINT_EQ(greet(instance, 9), 2);
    stop(running);
}

/* Three hits flatten into eighteen core values, so as an entry point's arguments they spill into
 * the script's memory, each handle with them, and the script passes them on to a host function as
 * Stasis laid them out, which Stasis reads them from. */
TEST(hits_spill_with_their_handles) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    ASSERT_INT_EQ(test_handles_npc_pass_hits(instance, hit_of(1, "a", 1, 1.0F, 2.0F),
                                             hit_of(2, "bb", 2, 3.0F, 4.0F),
                                             hit_of(1, "ccc", 3, 5.0F, 6.0F)),
                  STASIS_OK);
    ASSERT_SIZE_EQ(seen.hit_count, 3);
    expect_hit(seen.hits[0], 1, "a", 1, 1.0F, 2.0F);
    expect_hit(seen.hits[1], 2, "bb", 2, 3.0F, 4.0F);
    expect_hit(seen.hits[2], 1, "ccc", 3, 5.0F, 6.0F);
    ASSERT_STR_EQ(seen.weapons[2], "ccc");
    ASSERT_UINT_EQ(greet(instance, 1), 1);
    ASSERT_UINT_EQ(greet(instance, 2), 2);
    stop(running);
}

/* A host function whose result holds a null host object traps as a host abort, and an entry point
 * whose result holds a handle the instance was never given as a boundary violation, each with a
 * message that names the field. */
TEST(a_hit_whose_handle_cant_cross_traps) {
    const struct running running = start_handles();
    stasis_instance *instance = start_instance(running.script);
    ASSERT_UINT_EQ(find(instance, 0), 1);
    replies.hit = hit_of(0, "bow", 3, 0.0F, 0.0F);
    replies.hit.by = NULL;
    test_handles_combat_hit result;
    ASSERT_INT_EQ(test_handles_npc_fetch_hit(instance, 1, &result), STASIS_TRAP);
    ASSERT_TRUE(result.by == NULL);
    ASSERT_SIZE_EQ(result.weapon.size, 0);
    expect_trap(instance, STASIS_TRAP_KIND_HOST_ABORT,
                "the host function `combat.last_hit` returned the field `by` of its result as a "
                "null host object");

    instance = start_instance(running.script);
    ASSERT_INT_EQ(test_handles_npc_store(instance, 512, 9), STASIS_OK);
    ASSERT_INT_EQ(test_handles_npc_return_hit_at(instance, 512, &result), STASIS_TRAP);
    expect_trap(instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the entry point `npc.return_hit_at` returned the field `by` of its result as "
                "handle 9, which the instance was never given");
    stop(running);
}

/* The handle of the entity at `index` once every entity has been found, and those at even indices
 * revoked and found again, in order. */
static uint32_t handle_after_churn(uint32_t index) {
    return index % 2 == 0 ? HOST_OBJECT_COUNT + 1 + (index / 2) : index + 1;
}

/* An instance keeps each host object's handle apart from the others', however many it's given and
 * however many of them are revoked. */
TEST(keeps_many_handles_apart) {
    const struct running running = start_handles();
    stasis_instance *instance = running.instance;
    for (uint32_t i = 0; i < HOST_OBJECT_COUNT; i++) {
        ASSERT_UINT_EQ(find(instance, i), i + 1);
    }
    for (uint32_t i = 0; i < HOST_OBJECT_COUNT; i += 2) {
        revoke(instance, i);
    }
    for (uint32_t i = 0; i < HOST_OBJECT_COUNT; i++) {
        ASSERT_INT_EQ(test_handles_npc_touch(instance, i + 1), STASIS_OK);
        ASSERT_TRUE(seen.objects[0] == (i % 2 == 0 ? NULL : &entities[i]));
    }
    for (uint32_t i = 0; i < HOST_OBJECT_COUNT; i++) {
        ASSERT_UINT_EQ(find(instance, i), handle_after_churn(i));
    }
    for (uint32_t i = 0; i < HOST_OBJECT_COUNT; i++) {
        ASSERT_TRUE(choose(instance, handle_after_churn(i)) == &entities[i]);
    }
    stop(running);
}

TEST_LIST(TEST_CASE(issues_one_handle_for_each_host_object_in_order),
          TEST_CASE(each_instance_has_its_own_handles),
          TEST_CASE(a_handle_reaches_a_host_function_as_its_host_object),
          TEST_CASE(a_forged_handle_traps), TEST_CASE(a_handle_of_another_kind_traps),
          TEST_CASE(a_revoked_handle_reaches_host_functions_as_null),
          TEST_CASE(a_revoked_host_object_that_crosses_again_gets_a_new_handle),
          TEST_CASE(revokes_in_every_state),
          TEST_CASE(one_address_of_two_kinds_is_two_host_objects),
          TEST_CASE(refuses_bad_arguments_to_revoke),
          TEST_CASE(a_host_function_returning_a_null_host_object_traps),
          TEST_CASE(an_entry_point_takes_a_host_object_as_its_handle),
          TEST_CASE(refuses_a_null_host_object_as_an_entry_points_argument),
          TEST_CASE(an_entry_point_returns_a_handle_as_its_host_object),
          TEST_CASE(an_entry_point_returning_a_forged_handle_traps),
          TEST_CASE(the_dungeons_hit_crosses_with_its_handle),
          TEST_CASE(hits_spill_with_their_handles), TEST_CASE(a_hit_whose_handle_cant_cross_traps),
          TEST_CASE(keeps_many_handles_apart));
