/* Whether a loaded script is snapshottable, and if not, why: a script is snapshottable only if a
 * snapshot can hold everything its instances change. One that isn't still loads and runs.
 *
 * A shared memory, more than one memory, or an import that isn't a function would each make a
 * script unsnapshottable too, but no such script loads: WAMR refuses the first two, and loading
 * the last, as the load tests show. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_calls.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Loads the script <name>.wasm into `library` against calls.stasis, which must load as a
 * snapshottable script, with no reason. */
static stasis_script *load_snapshottable(stasis_library *library, const char *name) {
    stasis_script *script = load_script(library, &test_calls_interface, name);
    const char *reason = "not written";
    ASSERT_TRUE(stasis_script_snapshottable(script, &reason));
    ASSERT_TRUE(reason == NULL);
    ASSERT_TRUE(stasis_script_snapshottable(script, NULL));
    return script;
}

/* Loads the script <name>.wasm against calls.stasis, which must load as a snapshottable script. */
static void expect_snapshottable(const char *name) {
    stasis_library *library = create_library();
    stasis_script *script = load_snapshottable(library, name);
    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST(a_script_that_exports_its_memory_and_mutable_globals_is_snapshottable) {
    expect_snapshottable("calls");
}

/* Loads the script <name>.wasm against calls.stasis, which must load as a script that isn't
 * snapshottable, for the reason `expected`. */
static void expect_unsnapshottable(const char *name, const char *expected) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, name);
    const char *reason = NULL;
    ASSERT_TRUE(!stasis_script_snapshottable(script, &reason));
    ASSERT_TRUE(reason != NULL);
    ASSERT_STR_EQ(reason, expected);
    ASSERT_TRUE(!stasis_script_snapshottable(script, NULL));
    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* Reading the script's code finds none of the instructions that would make it unsnapshottable in
 * the immediates of the others, and none of its instructions is one the reader can't read. */
TEST(a_script_with_every_other_kind_of_instruction_is_snapshottable) {
    expect_snapshottable("instructions");
}

TEST(a_c_script_is_snapshottable) {
    stasis_library *library = create_library();
    stasis_script *script = load_snapshottable(library, "c_script");
    stasis_instance *instance = start_instance(script);
    uint32_t starts = 0;
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 1);
    destroy_all(instance, script, library);
}

TEST(a_script_without_a_memory_is_unsnapshottable) {
    expect_unsnapshottable("no_memory", "the script defines no memory");
}

TEST(a_memory_that_isnt_exported_makes_a_script_unsnapshottable) {
    expect_unsnapshottable("unexported_memory", "memory 0 isn't exported");
}

/* Each instruction is named once, with the first function that contains it. */
TEST(instructions_that_change_a_table_or_drop_a_segment_make_a_script_unsnapshottable) {
    expect_unsnapshottable("changes_tables",
                           "the script contains `table.set`, first in function 2\n"
                           "the script contains `table.grow`, first in function 3\n"
                           "the script contains `table.fill`, first in function 4\n"
                           "the script contains `table.copy`, first in function 5\n"
                           "the script contains `table.init`, first in function 6\n"
                           "the script contains `elem.drop`, first in function 7\n"
                           "the script contains `data.drop`, first in function 8");
}

TEST(a_mutable_global_that_isnt_exported_makes_a_script_unsnapshottable) {
    expect_unsnapshottable("unexported_global", "global 1 is mutable but not exported");
}

TEST(a_mutable_global_that_isnt_a_number_makes_a_script_unsnapshottable) {
    expect_unsnapshottable("reference_globals",
                           "global 0 is mutable but of type funcref, not i32, i64, f32 or f64\n"
                           "global 1 is mutable but of type externref, not i32, i64, f32 or f64");
}

/* The memory comes first, then the instructions, then the globals. */
TEST(lists_every_reason_a_script_isnt_snapshottable) {
    expect_unsnapshottable("unsnapshottable",
                           "memory 0 isn't exported\n"
                           "the script contains `table.grow`, first in function 0\n"
                           "global 0 is mutable but not exported\n"
                           "global 0 is mutable but of type externref, not i32, i64, f32 or f64");
}

TEST(a_null_script_isnt_snapshottable) {
    const char *reason = "not written";
    ASSERT_TRUE(!stasis_script_snapshottable(NULL, &reason));
    ASSERT_TRUE(reason == NULL);
    ASSERT_TRUE(!stasis_script_snapshottable(NULL, NULL));
}

/* Its load writes an empty message, as load_script checks, and its instances run. */
TEST(a_script_that_isnt_snapshottable_still_loads_and_runs) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "unexported_global");
    ASSERT_TRUE(!stasis_script_snapshottable(script, NULL));
    stasis_instance *instance = start_instance(script);
    uint32_t value = 0;
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &value), STASIS_OK);
    ASSERT_UINT_EQ(value, 7);
    ASSERT_INT_EQ(test_calls_caller_tick(instance, 9), STASIS_OK);
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &value), STASIS_OK);
    ASSERT_UINT_EQ(value, 9);
    destroy_all(instance, script, library);
}

TEST_LIST(
    TEST_CASE(a_script_that_exports_its_memory_and_mutable_globals_is_snapshottable),
    TEST_CASE(a_script_with_every_other_kind_of_instruction_is_snapshottable),
    TEST_CASE(a_c_script_is_snapshottable), TEST_CASE(a_script_without_a_memory_is_unsnapshottable),
    TEST_CASE(a_memory_that_isnt_exported_makes_a_script_unsnapshottable),
    TEST_CASE(instructions_that_change_a_table_or_drop_a_segment_make_a_script_unsnapshottable),
    TEST_CASE(a_mutable_global_that_isnt_exported_makes_a_script_unsnapshottable),
    TEST_CASE(a_mutable_global_that_isnt_a_number_makes_a_script_unsnapshottable),
    TEST_CASE(lists_every_reason_a_script_isnt_snapshottable),
    TEST_CASE(a_null_script_isnt_snapshottable),
    TEST_CASE(a_script_that_isnt_snapshottable_still_loads_and_runs));
