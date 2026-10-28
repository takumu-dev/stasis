/* Creating and destroying libraries, several at once included. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_calls.h"

#include <stddef.h>
#include <stdint.h>

TEST(creates_a_library_with_the_default_settings) {
    stasis_library *library = NULL;
    ASSERT_INT_EQ(stasis_library_create(NULL, &library), STASIS_OK);
    ASSERT_TRUE(library != NULL);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST(creates_a_library_with_settings_of_its_own) {
    stasis_library_settings settings = {0};
    settings.stack_size = (size_t)128 * 1024;
    stasis_library *library = NULL;
    ASSERT_INT_EQ(stasis_library_create(&settings, &library), STASIS_OK);
    ASSERT_TRUE(library != NULL);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST(refuses_a_stack_size_over_32_bits) {
    stasis_library_settings settings = {0};
    settings.stack_size = (size_t)UINT32_MAX + 1;
    stasis_library *library = create_library();
    stasis_library *refused = library;
    ASSERT_INT_EQ(stasis_library_create(&settings, &refused), STASIS_INVALID_ARGUMENT);
    ASSERT_TRUE(refused == NULL);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST(refuses_nowhere_to_put_the_library) {
    ASSERT_INT_EQ(stasis_library_create(NULL, NULL), STASIS_INVALID_ARGUMENT);
}

TEST(destroying_null_succeeds) { ASSERT_INT_EQ(stasis_library_destroy(NULL), STASIS_OK); }

TEST(refuses_to_destroy_a_library_that_has_scripts) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_REFUSED_IN_USE);
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_calls_caller_tick(instance, 1), STASIS_OK);
    destroy_all(instance, script, library);
}

/* WAMR's state belongs to the process, and lasts as long as any library does, whichever is
 * destroyed first. Once the last is gone, a new library starts it again. */
TEST(holds_several_libraries_at_once) {
    stasis_library *first = create_library();
    stasis_library *second = create_library();
    stasis_script *first_script = load_script(first, &test_calls_interface, "calls");
    stasis_script *second_script = load_script(second, &test_calls_interface, "calls");
    stasis_instance *first_instance = start_instance(first_script);
    stasis_instance *second_instance = start_instance(second_script);
    destroy_all(first_instance, first_script, first);

    ASSERT_INT_EQ(test_calls_caller_tick(second_instance, 2), STASIS_OK);
    uint64_t frame = 0;
    ASSERT_INT_EQ(test_calls_caller_last_frame(second_instance, &frame), STASIS_OK);
    ASSERT_UINT_EQ(frame, 2);
    stasis_script *later_script = load_script(second, &test_calls_interface, "older_minor");
    ASSERT_INT_EQ(stasis_script_destroy(later_script), STASIS_OK);
    destroy_all(second_instance, second_script, second);

    stasis_library *third = create_library();
    stasis_script *third_script = load_script(third, &test_calls_interface, "calls");
    stasis_instance *third_instance = start_instance(third_script);
    ASSERT_INT_EQ(test_calls_caller_tick(third_instance, 3), STASIS_OK);
    ASSERT_INT_EQ(test_calls_caller_last_frame(third_instance, &frame), STASIS_OK);
    ASSERT_UINT_EQ(frame, 3);
    destroy_all(third_instance, third_script, third);
}

TEST_LIST(TEST_CASE(creates_a_library_with_the_default_settings),
          TEST_CASE(creates_a_library_with_settings_of_its_own),
          TEST_CASE(refuses_a_stack_size_over_32_bits),
          TEST_CASE(refuses_nowhere_to_put_the_library), TEST_CASE(destroying_null_succeeds),
          TEST_CASE(refuses_to_destroy_a_library_that_has_scripts),
          TEST_CASE(holds_several_libraries_at_once));
