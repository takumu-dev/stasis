/* Creating, starting, calling and destroying script instances, and the states they go through:
 * created, then ready once started, and trapped for good once a call traps. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_calls.h"
#include "test_early.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

TEST(calls_an_entry_point_through_its_typed_caller) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_calls_caller_tick(instance, 42), STASIS_OK);
    uint64_t frame = 0;
    ASSERT_INT_EQ(test_calls_caller_last_frame(instance, &frame), STASIS_OK);
    ASSERT_UINT_EQ(frame, 42);
    destroy_all(instance, script, library);
}

TEST(calls_an_entry_point_through_the_generic_call) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = start_instance(script);
    const uint64_t frame = 7;
    ASSERT_INT_EQ(
        stasis_call(instance, &test_calls_interface, TEST_CALLS_CALLER_TICK_ENTRY, &frame, NULL),
        STASIS_OK);
    uint64_t last = 0;
    ASSERT_INT_EQ(stasis_call(instance, &test_calls_interface, TEST_CALLS_CALLER_LAST_FRAME_ENTRY,
                              NULL, &last),
                  STASIS_OK);
    ASSERT_UINT_EQ(last, 7);
    destroy_all(instance, script, library);
}

/* Creating runs nothing, and starting runs stasis_initialize once. */
TEST(starting_initialises_the_instance_once) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = NULL;
    ASSERT_INT_EQ(stasis_instance_create(script, NULL, &instance), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_OK);
    uint32_t starts = 0;
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 1);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_REFUSED_STARTED);
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 1);
    destroy_all(instance, script, library);
}

TEST(starts_a_script_without_stasis_initialize) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "older_minor");
    stasis_instance *instance = start_instance(script);
    uint32_t starts = 99;
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 0);
    destroy_all(instance, script, library);
}

/* Each instance has its own state. */
TEST(instances_of_one_script_are_apart) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *first = start_instance(script);
    stasis_instance *second = start_instance(script);
    ASSERT_INT_EQ(test_calls_caller_tick(first, 1), STASIS_OK);
    ASSERT_INT_EQ(test_calls_caller_tick(second, 2), STASIS_OK);
    uint64_t frame = 0;
    ASSERT_INT_EQ(test_calls_caller_last_frame(first, &frame), STASIS_OK);
    ASSERT_UINT_EQ(frame, 1);
    ASSERT_INT_EQ(test_calls_caller_last_frame(second, &frame), STASIS_OK);
    ASSERT_UINT_EQ(frame, 2);
    uint32_t starts = 0;
    ASSERT_INT_EQ(test_calls_caller_starts(second, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 1);
    ASSERT_INT_EQ(stasis_instance_destroy(first), STASIS_OK);
    destroy_all(second, script, library);
}

TEST(keeps_its_user_data) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    int entity = 0;
    stasis_instance *instance = NULL;
    ASSERT_INT_EQ(stasis_instance_create(script, &entity, &instance), STASIS_OK);
    ASSERT_TRUE(stasis_instance_user_data(instance) == &entity);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_OK);
    ASSERT_TRUE(stasis_instance_user_data(instance) == &entity);
    ASSERT_TRUE(stasis_instance_user_data(NULL) == NULL);
    destroy_all(instance, script, library);
}

/* A refused call zeroes the result. */
TEST(refuses_calls_before_it_starts) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = NULL;
    ASSERT_INT_EQ(stasis_instance_create(script, NULL, &instance), STASIS_OK);
    uint32_t starts = 99;
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &starts), STASIS_REFUSED_NOT_STARTED);
    ASSERT_UINT_EQ(starts, 0);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_OK);
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 1);
    destroy_all(instance, script, library);
}

TEST(refuses_an_optional_entry_point_the_script_lacks) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "older_minor");
    stasis_instance *instance = start_instance(script);
    uint8_t echoed = 99;
    ASSERT_INT_EQ(test_calls_caller_echo_u8(instance, 1, &echoed), STASIS_REFUSED_ABSENT);
    ASSERT_UINT_EQ(echoed, 0);
    ASSERT_INT_EQ(test_calls_caller_tick(instance, 1), STASIS_REFUSED_ABSENT);
    destroy_all(instance, script, library);
}

/* A trap ends the instance, which refuses every later call. */
TEST(a_trap_ends_the_instance) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_calls_caller_fail(instance), STASIS_TRAP);
    uint32_t starts = 99;
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &starts), STASIS_REFUSED_TRAPPED);
    ASSERT_UINT_EQ(starts, 0);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_REFUSED_TRAPPED);
    destroy_all(instance, script, library);
}

TEST(refuses_to_destroy_a_script_that_has_instances) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_REFUSED_IN_USE);
    ASSERT_INT_EQ(test_calls_caller_tick(instance, 3), STASIS_OK);
    stasis_instance *later = start_instance(script);
    ASSERT_INT_EQ(stasis_instance_destroy(later), STASIS_OK);
    destroy_all(instance, script, library);
}

TEST(destroying_null_succeeds) {
    ASSERT_INT_EQ(stasis_instance_destroy(NULL), STASIS_OK);
    ASSERT_INT_EQ(stasis_script_destroy(NULL), STASIS_OK);
}

TEST(refuses_bad_arguments_to_create_and_start) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = start_instance(script);
    stasis_instance *refused = instance;
    ASSERT_INT_EQ(stasis_instance_create(NULL, NULL, &refused), STASIS_INVALID_ARGUMENT);
    ASSERT_TRUE(refused == NULL);
    ASSERT_INT_EQ(stasis_instance_create(script, NULL, NULL), STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_instance_start(NULL), STASIS_INVALID_ARGUMENT);
    destroy_all(instance, script, library);
}

/* Each refused call zeroes the result, where there's one. */
TEST(refuses_bad_arguments_to_a_call) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = start_instance(script);
    const uint8_t value = 5;
    uint8_t result = 99;

    ASSERT_INT_EQ(test_calls_caller_echo_u8(NULL, value, &result), STASIS_INVALID_ARGUMENT);
    ASSERT_UINT_EQ(result, 0);
    ASSERT_INT_EQ(stasis_call(instance, NULL, TEST_CALLS_CALLER_ECHO_U8_ENTRY, &value, &result),
                  STASIS_INVALID_ARGUMENT);

    /* Another interface, even one with an entry point of that id. */
    uint32_t starts = 99;
    ASSERT_INT_EQ(
        stasis_call(instance, &test_early_interface, TEST_EARLY_CALLER_STARTS_ENTRY, NULL, &starts),
        STASIS_INVALID_ARGUMENT);
    ASSERT_UINT_EQ(starts, 0);

    ASSERT_INT_EQ(stasis_call(instance, &test_calls_interface, 1000, &value, &result),
                  STASIS_INVALID_ARGUMENT);
    result = 99;
    ASSERT_INT_EQ(stasis_call(instance, &test_calls_interface, TEST_CALLS_CALLER_ECHO_U8_ENTRY,
                              NULL, &result),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_UINT_EQ(result, 0);
    ASSERT_INT_EQ(
        stasis_call(instance, &test_calls_interface, TEST_CALLS_CALLER_ECHO_U8_ENTRY, &value, NULL),
        STASIS_INVALID_ARGUMENT);

    /* What can't cross yet: a handle, even beside records and a string that can. */
    test_calls_world_point point = {1.0F, 2.0F};
    test_calls_world_area within = {{0.0F, 0.0F}, {1.0F, 1.0F}, {"here", 4}};
    ASSERT_INT_EQ(test_calls_caller_place(instance, within, NULL, &point), STASIS_INVALID_ARGUMENT);
    ASSERT_TRUE(point.x == 0.0F && point.y == 0.0F);

    /* None of them reached the instance, which still works. */
    ASSERT_INT_EQ(test_calls_caller_echo_u8(instance, value, &result), STASIS_OK);
    ASSERT_UINT_EQ(result, 5);
    destroy_all(instance, script, library);
}

TEST_LIST(TEST_CASE(calls_an_entry_point_through_its_typed_caller),
          TEST_CASE(calls_an_entry_point_through_the_generic_call),
          TEST_CASE(starting_initialises_the_instance_once),
          TEST_CASE(starts_a_script_without_stasis_initialize),
          TEST_CASE(instances_of_one_script_are_apart), TEST_CASE(keeps_its_user_data),
          TEST_CASE(refuses_calls_before_it_starts),
          TEST_CASE(refuses_an_optional_entry_point_the_script_lacks),
          TEST_CASE(a_trap_ends_the_instance),
          TEST_CASE(refuses_to_destroy_a_script_that_has_instances),
          TEST_CASE(destroying_null_succeeds), TEST_CASE(refuses_bad_arguments_to_create_and_start),
          TEST_CASE(refuses_bad_arguments_to_a_call));
