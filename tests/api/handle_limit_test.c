/* Instances running out of handles, which only the test-only library lets a test reach: it lowers
 * the most handles an instance can be given over its life, which in the library is every 32-bit
 * value but 0. */

#include <stasis.h>

#include "handle_hosts.h"
#include "harness.h"
#include "support.h"
#include "test_handles.h"
#include "testing.h"

#include <stdint.h>
#include <stdio.h>

/* The handle that `find` gives the instance for the entity at `index`. */
static uint32_t find(stasis_instance *instance, uint32_t index) {
    uint32_t handle = 0;
    ASSERT_INT_EQ(test_handles_npc_find(instance, index, &handle), STASIS_OK);
    return handle;
}

/* Checks that `instance` ran out of handles as it needed one for `value`, as the trap's message
 * names it, and destroys it. */
static void expect_exhausted(stasis_instance *instance, const char *value) {
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_HANDLES_EXHAUSTED);
    char message[256];
    const int length = snprintf(message, sizeof message,
                                "the instance has been given all %lu handles it can have, and "
                                "needs another for %s",
                                (unsigned long)stasis_testing_handle_limit(), value);
    ASSERT_TRUE(length > 0 && (size_t)length < sizeof message);
    ASSERT_STR_EQ(trap->message, message);
    ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_OK);
}

/* An instance that has been given every handle it can have traps as handles_exhausted at the first
 * call that needs another, for a host function's result or an entry point's argument. Until then,
 * host objects with handles still cross, and revoking a handle gives none back, since handles are
 * never reused. */
TEST(running_out_of_handles_traps) {
    const uint32_t limit = stasis_testing_handle_limit();
    ASSERT_TRUE(limit >= 2 && limit < HOST_OBJECT_COUNT);
    forget_seen();
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_handles_interface, "handles");

    stasis_instance *instance = start_instance(script);
    for (uint32_t i = 0; i < limit; i++) {
        ASSERT_UINT_EQ(find(instance, i), i + 1);
    }
    ASSERT_INT_EQ(stasis_instance_revoke(instance, TEST_HANDLES_WORLD_ENTITY_KIND, &entities[0]),
                  STASIS_OK);
    ASSERT_UINT_EQ(find(instance, 1), 2);
    uint32_t handle = 99;
    ASSERT_INT_EQ(test_handles_npc_find(instance, 0, &handle), STASIS_TRAP);
    ASSERT_UINT_EQ(handle, 0);
    expect_exhausted(instance, "the result of `world.find`");

    instance = start_instance(script);
    for (uint32_t i = 0; i < limit; i++) {
        ASSERT_INT_EQ(test_handles_npc_greet(instance, &entities[i], &handle), STASIS_OK);
        ASSERT_UINT_EQ(handle, i + 1);
    }
    ASSERT_INT_EQ(test_handles_npc_greet(instance, &entities[limit], &handle), STASIS_TRAP);
    expect_exhausted(instance, "argument 1 of `npc.greet`");

    instance = start_instance(script);
    for (uint32_t i = 0; i < limit; i++) {
        ASSERT_UINT_EQ(find(instance, i), i + 1);
    }
    replies.hit.by = &entities[limit];
    test_handles_combat_hit result;
    ASSERT_INT_EQ(test_handles_npc_fetch_hit(instance, 1, &result), STASIS_TRAP);
    expect_exhausted(instance, "the field `by` of the result of `combat.last_hit`");

    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
    forget_seen();
}

TEST_LIST(TEST_CASE(running_out_of_handles_traps));
