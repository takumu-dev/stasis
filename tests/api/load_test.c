/* Loading scripts: the copy Stasis makes of a module's bytes, the identity it computes, each stage
 * and problem of the load check with its status and message, and what a loaded script answers. */

#include <stasis.h>
#include <stasis_interface.h>

#include "harness.h"
#include "sha256.h"
#include "support.h"
#include "test_calls.h"
#include "test_early.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Loads the script <name>.wasm against `interface_data`, which must fail with `status` and the
 * message `expected`, leaving no script behind. */
static void expect_refused(const stasis_interface *interface_data, const char *name,
                           stasis_status status, const char *expected) {
    stasis_library *library = create_library();
    const struct module_bytes module = read_module(name);
    char message[2048] = "not written";
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, interface_data, module.bytes, module.size, message,
                                     sizeof message, &script),
                  status);
    ASSERT_STR_EQ(message, expected);
    ASSERT_TRUE(script == NULL);
    free_module(module);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST(loads_a_script_and_writes_an_empty_message) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* calls.wasm provides every entry point, `place` among them, whose signature takes records, a
 * string and a handle, so its name's hash matches only if the core encodes them all as the
 * generator does. */
TEST(answers_which_entry_points_it_provides) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    ASSERT_TRUE(stasis_script_provides(script, TEST_CALLS_CALLER_STARTS_ENTRY));
    ASSERT_TRUE(stasis_script_provides(script, TEST_CALLS_CALLER_ECHO_U8_ENTRY));
    ASSERT_TRUE(stasis_script_provides(script, TEST_CALLS_CALLER_MIX_ENTRY));
    ASSERT_TRUE(stasis_script_provides(script, TEST_CALLS_CALLER_PLACE_ENTRY));
    ASSERT_TRUE(!stasis_script_provides(script, TEST_CALLS_CALLER_PLACE_ENTRY + 1));
    ASSERT_TRUE(!stasis_script_provides(NULL, TEST_CALLS_CALLER_STARTS_ENTRY));
    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* A script built before the optional entry points were added provides only the required one. */
TEST(loads_a_script_built_against_an_older_minor_version) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "older_minor");
    ASSERT_TRUE(stasis_script_provides(script, TEST_CALLS_CALLER_STARTS_ENTRY));
    ASSERT_TRUE(!stasis_script_provides(script, TEST_CALLS_CALLER_ECHO_U8_ENTRY));
    ASSERT_TRUE(!stasis_script_provides(script, TEST_CALLS_CALLER_PLACE_ENTRY));
    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* The entry point a newer minor version added is ignored, as are exports Stasis gives no meaning
 * to. */
TEST(loads_a_script_built_against_a_newer_minor_version) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "newer_minor");
    stasis_instance *instance = start_instance(script);
    uint32_t starts = 99;
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 0);
    destroy_all(instance, script, library);
}

/* Under 0.x, the line is the major and minor version, and the patch version doesn't count. */
TEST(loads_a_script_of_the_same_minor_version_under_0x) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_early_interface, "early");
    stasis_instance *instance = start_instance(script);
    uint32_t starts = 0;
    ASSERT_INT_EQ(test_early_caller_starts(instance, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 3);
    destroy_all(instance, script, library);
}

TEST(holds_scripts_of_several_interfaces) {
    stasis_library *library = create_library();
    stasis_script *calls = load_script(library, &test_calls_interface, "calls");
    stasis_script *early = load_script(library, &test_early_interface, "early");
    stasis_instance *calls_instance = start_instance(calls);
    stasis_instance *early_instance = start_instance(early);
    uint32_t starts = 0;
    ASSERT_INT_EQ(test_calls_caller_starts(calls_instance, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 1);
    ASSERT_INT_EQ(test_early_caller_starts(early_instance, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 3);
    ASSERT_INT_EQ(stasis_instance_destroy(early_instance), STASIS_OK);
    ASSERT_INT_EQ(stasis_script_destroy(early), STASIS_OK);
    destroy_all(calls_instance, calls, library);
}

/* Stasis keeps a copy of the bytes, so the host's may change or go once loading returns. */
TEST(the_host_may_free_its_bytes_once_loading_returns) {
    stasis_library *library = create_library();
    struct module_bytes module = read_module("calls");
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, &test_calls_interface, module.bytes, module.size,
                                     NULL, 0, &script),
                  STASIS_OK);
    memset(module.bytes, 0xFF, module.size);
    free_module(module);
    stasis_instance *instance = start_instance(script);
    uint8_t echoed = 0;
    ASSERT_INT_EQ(test_calls_caller_echo_u8(instance, 9, &echoed), STASIS_OK);
    ASSERT_UINT_EQ(echoed, 9);
    destroy_all(instance, script, library);
}

/* A script's identity is the SHA-256 of its sections without the custom ones, in order, which a
 * copy stripped of them holds after the module's 8-byte header. */
TEST(identifies_a_script_by_its_sections_without_the_custom_ones) {
    stasis_library *library = create_library();
    stasis_script *full = load_script(library, &test_calls_interface, "identity");
    stasis_script *stripped = load_script(library, &test_calls_interface, "identity_stripped");
    stasis_script *other = load_script(library, &test_calls_interface, "newer_minor");

    const struct module_bytes module = read_module("identity_stripped");
    ASSERT_TRUE(module.size > 8);
    stasis_sha256 sha;
    stasis_sha256_init(&sha);
    stasis_sha256_update(&sha, module.bytes + 8, module.size - 8);
    uint8_t expected[STASIS_SHA256_SIZE];
    stasis_sha256_final(&sha, expected);
    free_module(module);

    ASSERT_MEM_EQ(stasis_script_identity(full), expected, STASIS_IDENTITY_SIZE);
    ASSERT_MEM_EQ(stasis_script_identity(stripped), expected, STASIS_IDENTITY_SIZE);
    ASSERT_TRUE(memcmp(stasis_script_identity(other), expected, STASIS_IDENTITY_SIZE) != 0);
    ASSERT_TRUE(stasis_script_identity(NULL) == NULL);

    ASSERT_INT_EQ(stasis_script_destroy(full), STASIS_OK);
    ASSERT_INT_EQ(stasis_script_destroy(stripped), STASIS_OK);
    ASSERT_INT_EQ(stasis_script_destroy(other), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* The message is WAMR's loader's. */
TEST(refuses_bytes_that_arent_webassembly) {
    stasis_library *library = create_library();
    const char bytes[] = "not a WebAssembly module";
    char message[256] = "not written";
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, &test_calls_interface, bytes, sizeof bytes, message,
                                     sizeof message, &script),
                  STASIS_INVALID_MODULE);
    ASSERT_STR_EQ(message, "WASM module load failed: magic header not detected");
    ASSERT_TRUE(script == NULL);
    ASSERT_INT_EQ(stasis_script_load(library, &test_calls_interface, NULL, 0, message,
                                     sizeof message, &script),
                  STASIS_INVALID_MODULE);
    ASSERT_TRUE(script == NULL);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST(refuses_a_truncated_module) {
    stasis_library *library = create_library();
    const struct module_bytes module = read_module("calls");
    char message[256] = "";
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, &test_calls_interface, module.bytes, module.size / 2,
                                     message, sizeof message, &script),
                  STASIS_INVALID_MODULE);
    ASSERT_TRUE(strncmp(message, "WASM module load failed: ", 25) == 0);
    ASSERT_TRUE(script == NULL);
    free_module(module);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST(refuses_a_script_built_for_another_interface) {
    expect_refused(&test_calls_interface, "other_interface", STASIS_WRONG_INTERFACE,
                   "the script was built for the interface `other:calls`, not "
                   "`test:calls@1.2.0`, as its export "
                   "`other:calls/caller@1#starts:5b69886a8b9e31a8` says");
}

TEST(refuses_an_import_from_another_interface) {
    expect_refused(&test_calls_interface, "other_interface_import", STASIS_WRONG_INTERFACE,
                   "the script was built for the interface `other:calls`, not "
                   "`test:calls@1.2.0`, as its import module `other:calls/world@1` says");
}

TEST(refuses_another_major_version) {
    expect_refused(&test_calls_interface, "next_major", STASIS_INCOMPATIBLE_VERSION,
                   "the script was built for `test:calls@2`, which isn't compatible with "
                   "`test:calls@1.2.0`, as its export "
                   "`test:calls/caller@2#starts:5b69886a8b9e31a8` says");
}

TEST(refuses_another_minor_version_under_0x) {
    expect_refused(&test_early_interface, "early_other_minor", STASIS_INCOMPATIBLE_VERSION,
                   "the script was built for `test:early@0.2`, which isn't compatible with "
                   "`test:early@0.3.1`, as its export "
                   "`test:early/caller@0.2#starts:5b69886a8b9e31a8` says");
}

/* The interface is checked before the line: calls.wasm is for `test:calls`. */
TEST(refuses_a_script_for_another_interface_before_its_version) {
    expect_refused(&test_early_interface, "calls", STASIS_WRONG_INTERFACE,
                   "the script was built for the interface `test:calls`, not "
                   "`test:early@0.3.1`, as its export "
                   "`test:calls/caller@1#starts:5b69886a8b9e31a8` says");
}

TEST(refuses_a_script_that_lacks_a_required_entry_point) {
    expect_refused(&test_calls_interface, "missing_required", STASIS_INTERFACE_MISMATCH,
                   "the script lacks the entry point `caller.starts`, which `test:calls@1.2.0` "
                   "requires");
}

TEST(refuses_an_entry_point_with_another_signature) {
    expect_refused(&test_calls_interface, "other_signature", STASIS_INTERFACE_MISMATCH,
                   "the script's entry point `caller.starts` has another signature than in "
                   "`test:calls@1.2.0`");
}

/* Past the stages, every problem is listed, and the status is the first one's. */
TEST(lists_every_problem_with_the_first_ones_status) {
    expect_refused(&test_calls_interface, "mismatches", STASIS_INTERFACE_MISMATCH,
                   "the script lacks the entry point `caller.starts`, which `test:calls@1.2.0` "
                   "requires\n"
                   "the script's entry point `caller.echo_u8` has another signature than in "
                   "`test:calls@1.2.0`\n"
                   "the export `test:calls/caller@1#Tick:37ab64a28242fa4b` doesn't parse as the "
                   "name of an entry point");
}

TEST(refuses_names_that_dont_parse_or_disagree) {
    expect_refused(
        &test_calls_interface, "bad_names", STASIS_INVALID_SCRIPT,
        "the import module `test:calls/world` doesn't parse as the name of a group of an "
        "interface\n"
        "the export `test:calls/caller@1.2.0#tick:37ab64a28242fa4b` doesn't parse as the name of "
        "an entry point\n"
        "the export `test:calls/caller@1#tick` doesn't parse as the name of an entry point\n"
        "the export `foo:bar` doesn't parse as the name of an entry point\n"
        "the script's entry points are of more than one kind of script: `caller` and `boss`\n"
        "the script exports the entry point `caller.echo_u8` more than once, as "
        "`test:calls/caller@1#echo_u8:4618eff65f831824` and "
        "`test:calls/caller@1#echo_u8:1a6e70e5cfc850c4`");
}

TEST(refuses_exports_that_stasis_cant_call) {
    expect_refused(&test_calls_interface, "wrong_types", STASIS_INVALID_SCRIPT,
                   "the script's entry point `caller.starts` is a function of type (i32) -> "
                   "(i32), where its signature makes it () -> (i32)\n"
                   "the script's entry point `caller.tick` is exported as something other than "
                   "a function\n"
                   "the script's `stasis_initialize` isn't a function that takes and returns "
                   "nothing");
}

TEST(refuses_a_start_section) {
    expect_refused(&test_calls_interface, "start_section", STASIS_INVALID_SCRIPT,
                   "the script has a start section, which a backend runs whenever it creates an "
                   "instance");
}

TEST(refuses_exports_that_a_backend_runs_by_itself) {
    expect_refused(&test_calls_interface, "backend_exports", STASIS_INVALID_SCRIPT,
                   "the script exports `_initialize`, which a backend runs by itself when it "
                   "creates an instance\n"
                   "the script exports `__wasm_call_ctors`, which a backend runs by itself when "
                   "it creates an instance\n"
                   "the script exports `__post_instantiate`, which a backend runs by itself when "
                   "it creates an instance");
}

TEST(refuses_imports_that_arent_functions) {
    expect_refused(&test_calls_interface, "non_function_imports", STASIS_INVALID_SCRIPT,
                   "the script imports the global `count` from `test:calls/world@1`, where a "
                   "script may import only functions\n"
                   "the script imports the table `things` from `test:calls/world@1`, where a "
                   "script may import only functions\n"
                   "the script imports the memory `memory` from `test:calls/world@1`, where a "
                   "script may import only functions");
}

TEST(refuses_imports_from_other_modules) {
    expect_refused(&test_calls_interface, "foreign_imports", STASIS_INVALID_SCRIPT,
                   "the script imports `log` from `env`, which is neither a group of "
                   "`test:calls@1.2.0` nor `wasi_snapshot_preview1`\n"
                   "the script imports `proc_exit` from `wasi_unstable`, which is neither a group "
                   "of `test:calls@1.2.0` nor `wasi_snapshot_preview1`");
}

TEST(refuses_a_memory_that_starts_with_more_than_65535_pages) {
    expect_refused(&test_calls_interface, "memory_too_large", STASIS_INVALID_SCRIPT,
                   "the script's memory 0 starts with 65536 pages, more than the 65535 an "
                   "instance may have");
}

/* Instantiating the script would copy each active segment into its memory or table, which has its
 * minimum size until the script grows it. Passive and declared segments are never copied. */
TEST(refuses_active_segments_that_dont_fit) {
    expect_refused(&test_calls_interface, "segments_dont_fit", STASIS_INVALID_SCRIPT,
                   "the script's data segment 1 needs its memory 0 to hold 65537 bytes, but it "
                   "starts with 65536\n"
                   "the script's data segment 3 needs its memory 0 to hold 4294967296 bytes, but "
                   "it starts with 65536\n"
                   "the script's element segment 1 needs its table 0 to hold 3 elements, but it "
                   "starts with 2\n"
                   "the script's element segment 2 needs its table 0 to hold 3 elements, but it "
                   "starts with 2");
}

/* Segments that end where the memory or the table does fit, so an instance of the script can be
 * created, with the data in its memory. */
TEST(loads_active_segments_that_just_fit) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "segments_fit");
    stasis_instance *instance = start_instance(script);
    uint32_t last = 0;
    ASSERT_INT_EQ(test_calls_caller_starts(instance, &last), STASIS_OK);
    ASSERT_UINT_EQ(last, 's');
    destroy_all(instance, script, library);
}

/* The rules come after the signatures and the names, in the order the message lists them, so the
 * status is the lacking entry point's. */
TEST(lists_every_broken_rule_after_the_signatures_and_names) {
    expect_refused(&test_calls_interface, "broken_rules", STASIS_INTERFACE_MISMATCH,
                   "the script lacks the entry point `caller.starts`, which `test:calls@1.2.0` "
                   "requires\n"
                   "the export `test:calls/caller@1#Echo_u8:4618eff65f831824` doesn't parse as "
                   "the name of an entry point\n"
                   "the script has a start section, which a backend runs whenever it creates an "
                   "instance\n"
                   "the script exports `__wasm_call_ctors`, which a backend runs by itself when "
                   "it creates an instance\n"
                   "the script imports the global `seed` from `env`, where a script may import "
                   "only functions\n"
                   "the script imports `seed` from `env`, which is neither a group of "
                   "`test:calls@1.2.0` nor `wasi_snapshot_preview1`\n"
                   "the script's element segment 0 needs its table 0 to hold 2 elements, but it "
                   "starts with 1\n"
                   "the script's memory 0 starts with 65536 pages, more than the 65535 an "
                   "instance may have");
}

/* A script with a shared memory, or with more than one memory, wouldn't be snapshottable. WAMR, as
 * Stasis builds it, doesn't take either for valid WebAssembly. */
TEST(refuses_a_shared_memory) {
    expect_refused(&test_calls_interface, "shared_memory", STASIS_INVALID_MODULE,
                   "WASM module load failed: invalid limits flags");
}

TEST(refuses_more_than_one_memory) {
    expect_refused(&test_calls_interface, "two_memories", STASIS_INVALID_MODULE,
                   "WASM module load failed: multiple memories");
}

/* A message that doesn't fit is cut short, and still ends in a NUL. */
TEST(cuts_a_message_short_where_the_buffer_ends) {
    stasis_library *library = create_library();
    const struct module_bytes module = read_module("other_interface");
    char message[17];
    memset(message, 'x', sizeof message);
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, &test_calls_interface, module.bytes, module.size,
                                     message, 16, &script),
                  STASIS_WRONG_INTERFACE);
    ASSERT_STR_EQ(message, "the script was ");
    ASSERT_INT_EQ(message[16], 'x');
    ASSERT_INT_EQ(stasis_script_load(library, &test_calls_interface, module.bytes, module.size,
                                     message, 0, &script),
                  STASIS_WRONG_INTERFACE);
    ASSERT_INT_EQ(message[0], 't');
    free_module(module);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* Data in a descriptor format newer than this release reads is a bad argument, since a build
 * mixed up two releases. */
TEST(refuses_a_newer_descriptor_format) {
    stasis_library *library = create_library();
    const struct module_bytes module = read_module("calls");
    stasis_interface newer = test_calls_interface;
    newer.format = STASIS_INTERFACE_FORMAT + 1;
    char message[64] = "not written";
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, &newer, module.bytes, module.size, message,
                                     sizeof message, &script),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_STR_EQ(message, "");
    ASSERT_TRUE(script == NULL);
    free_module(module);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST(refuses_bad_arguments) {
    stasis_library *library = create_library();
    const struct module_bytes module = read_module("calls");
    const stasis_interface *calls = &test_calls_interface;
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(NULL, calls, module.bytes, module.size, NULL, 0, &script),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_TRUE(script == NULL);
    ASSERT_INT_EQ(stasis_script_load(library, NULL, module.bytes, module.size, NULL, 0, &script),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_script_load(library, calls, NULL, module.size, NULL, 0, &script),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_script_load(library, calls, module.bytes, module.size, NULL, 16, &script),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_script_load(library, calls, module.bytes, module.size, NULL, 0, NULL),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_TRUE(script == NULL);
    free_module(module);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST_LIST(TEST_CASE(loads_a_script_and_writes_an_empty_message),
          TEST_CASE(answers_which_entry_points_it_provides),
          TEST_CASE(loads_a_script_built_against_an_older_minor_version),
          TEST_CASE(loads_a_script_built_against_a_newer_minor_version),
          TEST_CASE(loads_a_script_of_the_same_minor_version_under_0x),
          TEST_CASE(holds_scripts_of_several_interfaces),
          TEST_CASE(the_host_may_free_its_bytes_once_loading_returns),
          TEST_CASE(identifies_a_script_by_its_sections_without_the_custom_ones),
          TEST_CASE(refuses_bytes_that_arent_webassembly), TEST_CASE(refuses_a_truncated_module),
          TEST_CASE(refuses_a_script_built_for_another_interface),
          TEST_CASE(refuses_an_import_from_another_interface),
          TEST_CASE(refuses_another_major_version),
          TEST_CASE(refuses_another_minor_version_under_0x),
          TEST_CASE(refuses_a_script_for_another_interface_before_its_version),
          TEST_CASE(refuses_a_script_that_lacks_a_required_entry_point),
          TEST_CASE(refuses_an_entry_point_with_another_signature),
          TEST_CASE(lists_every_problem_with_the_first_ones_status),
          TEST_CASE(refuses_names_that_dont_parse_or_disagree),
          TEST_CASE(refuses_exports_that_stasis_cant_call), TEST_CASE(refuses_a_start_section),
          TEST_CASE(refuses_exports_that_a_backend_runs_by_itself),
          TEST_CASE(refuses_imports_that_arent_functions),
          TEST_CASE(refuses_imports_from_other_modules),
          TEST_CASE(refuses_a_memory_that_starts_with_more_than_65535_pages),
          TEST_CASE(refuses_active_segments_that_dont_fit),
          TEST_CASE(loads_active_segments_that_just_fit),
          TEST_CASE(lists_every_broken_rule_after_the_signatures_and_names),
          TEST_CASE(refuses_a_shared_memory), TEST_CASE(refuses_more_than_one_memory),
          TEST_CASE(cuts_a_message_short_where_the_buffer_ends),
          TEST_CASE(refuses_a_newer_descriptor_format), TEST_CASE(refuses_bad_arguments));
