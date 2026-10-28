/* A script instance's linear memory, which reads as zeros wherever the script hasn't written it,
 * when the instance is created and when its memory grows, whatever the memory it's made of held
 * before. Each case first fills an instance's memory and destroys it, so that a later instance's
 * memory may be made of what the first one's was. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_memory.h"

#include <stdint.h>

/* Starts an instance of `script`, fills its memory, and destroys it. */
static void leave_memory_filled(stasis_script *script) {
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_memory_caller_fill(instance), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_OK);
}

TEST(a_new_instance_starts_with_zeroed_memory) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_memory_interface, "memory");
    leave_memory_filled(script);
    stasis_instance *instance = start_instance(script);
    uint32_t nonzero = 1;
    ASSERT_INT_EQ(test_memory_caller_nonzero_bytes(instance, &nonzero), STASIS_OK);
    ASSERT_UINT_EQ(nonzero, 0);
    destroy_all(instance, script, library);
}

/* The page the instance starts with is filled before its memory grows, and the pages it grows by
 * are zero. */
TEST(memory_grows_zeroed) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_memory_interface, "memory");
    leave_memory_filled(script);
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_memory_caller_fill(instance), STASIS_OK);
    uint32_t pages = 0;
    ASSERT_INT_EQ(test_memory_caller_grow(instance, 3, &pages), STASIS_OK);
    ASSERT_UINT_EQ(pages, 1);
    uint32_t nonzero = 0;
    ASSERT_INT_EQ(test_memory_caller_nonzero_bytes(instance, &nonzero), STASIS_OK);
    ASSERT_UINT_EQ(nonzero, 65536);
    destroy_all(instance, script, library);
}

TEST_LIST(TEST_CASE(a_new_instance_starts_with_zeroed_memory), TEST_CASE(memory_grows_zeroed));
