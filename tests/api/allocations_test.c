/* What calls into a script allocate, as the test-only library counts it: Stasis copies an entry
 * point's string results into a buffer that the instance keeps, so once the first few calls have
 * made it large enough, calls allocate nothing. */

#include <stasis.h>

#include "harness.h"
#include "string_hosts.h"
#include "support.h"
#include "test_strings.h"
#include "testing.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* `text` as a stasis_string, without its NUL. */
static stasis_string text_of(const char *text) {
    stasis_string string;
    string.data = text;
    string.size = strlen(text);
    return string;
}

/* Strings cross both ways: to an entry point and back, and from a host function. */
static void call(stasis_instance *instance, const char *text) {
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_echo(instance, text_of(text), &result), STASIS_OK);
    ASSERT_SIZE_EQ(result.size, strlen(text));
    ASSERT_INT_EQ(test_strings_user_relay(instance, text_of(text), &result), STASIS_OK);
    ASSERT_SIZE_EQ(result.size, reply.size);
}

TEST(calls_with_strings_stop_allocating) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_strings_interface, "strings");
    stasis_instance *instance = start_instance(script);
    reply = text_of("a reply from the host");
    call(instance, "the longest string, which the buffer grows to hold");
    const size_t allocations = stasis_testing_allocations();
    for (int i = 0; i < 100; i++) {
        call(instance, i % 2 == 0 ? "short" : "a little longer");
        call(instance, "the longest string, which the buffer grows to hold");
    }
    ASSERT_SIZE_EQ(stasis_testing_allocations(), allocations);
    forget_taken();
    destroy_all(instance, script, library);
}

TEST_LIST(TEST_CASE(calls_with_strings_stop_allocating));
