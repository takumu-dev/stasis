/* A C script built against the C bindings of shapes.stasis alone, through which values of each
 * shape that the bindings lower or lift in a way of their own cross both ways: arguments that
 * spill, results in return areas, strings deep in records, and records of one value. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_shapes.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The test's one host object of the kind host.item. */
static int item;

/* The strings of the pairs that `host.take` was last passed, each as a C string, and the value
 * that `host.tag` was last passed. */
static char taken[5][2][32];
static double tagged_value;

/* What the script printed to stdout. */
static char output[256];
static size_t output_size;

static void record_output(stasis_instance *instance, void *user_data, stasis_stream stream,
                          const char *bytes, size_t size) {
    (void)instance;
    (void)user_data;
    ASSERT_INT_EQ(stream, STASIS_STREAM_STDOUT);
    ASSERT_TRUE(size < sizeof output - output_size);
    memcpy(output + output_size, bytes, size);
    output_size += size;
    output[output_size] = '\0';
}

/* Copies a string into `copy`, as a C string. */
static void copy_string(char copy[32], stasis_string string) {
    ASSERT_TRUE(string.size < 32);
    memcpy(copy, string.data, string.size);
    copy[string.size] = '\0';
}

test_shapes_host_wrapped test_shapes_host_take(stasis_instance *instance, test_shapes_host_pair a,
                                               test_shapes_host_pair b, test_shapes_host_pair c,
                                               test_shapes_host_pair d, test_shapes_host_pair e) {
    (void)instance;
    const test_shapes_host_pair pairs[5] = {a, b, c, d, e};
    size_t size = 0;
    for (size_t i = 0; i < 5; i++) {
        copy_string(taken[i][0], pairs[i].left.text);
        copy_string(taken[i][1], pairs[i].right);
        size += pairs[i].left.text.size + pairs[i].right.size;
    }
    test_shapes_host_wrapped result;
    result.inner.value = (double)size;
    return result;
}

test_shapes_host_pair test_shapes_host_give(stasis_instance *instance) {
    (void)instance;
    test_shapes_host_pair pair;
    pair.left.text.data = "the given left";
    pair.left.text.size = strlen(pair.left.text.data);
    pair.right.data = "the given right";
    pair.right.size = strlen(pair.right.data);
    return pair;
}

test_shapes_host_tagged test_shapes_host_tag(stasis_instance *instance,
                                             test_shapes_host_wrapped value) {
    (void)instance;
    tagged_value = value.inner.value;
    test_shapes_host_tagged tagged;
    tagged.item = (test_shapes_host_item *)(void *)&item;
    return tagged;
}

/* The script never calls it, since its kind of script doesn't use its group, and its C bindings
 * leave it out. */
void test_shapes_unused_ignore(stasis_instance *instance, test_shapes_unused_ignored value) {
    (void)instance;
    (void)value;
    ASSERT_TRUE(false);
}

/* An instance of shapes.wasm, started, with its script and library. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

/* Starts an instance of shapes.wasm in a library of its own, which sends the script's output to
 * record_output, having forgotten what earlier cases recorded. */
static struct running start(void) {
    output_size = 0;
    output[0] = '\0';
    memset(taken, 0, sizeof taken);
    tagged_value = 0.0;
    stasis_library_settings settings;
    memset(&settings, 0, sizeof settings);
    settings.output = record_output;
    struct running running;
    ASSERT_INT_EQ(stasis_library_create(&settings, &running.library), STASIS_OK);
    running.script = load_script(running.library, &test_shapes_interface, "shapes");
    running.instance = start_instance(running.script);
    return running;
}

static void stop(struct running running) {
    destroy_all(running.instance, running.script, running.library);
}

/* A pair of the strings `left` and `right`. */
static test_shapes_host_pair pair_of(const char *left, const char *right) {
    test_shapes_host_pair pair;
    pair.left.text.data = left;
    pair.left.text.size = strlen(left);
    pair.right.data = right;
    pair.right.size = strlen(right);
    return pair;
}

/* Fails unless a string holds the C string `expected`. */
static void expect_string(stasis_string actual, const char *expected) {
    ASSERT_SIZE_EQ(actual.size, strlen(expected));
    ASSERT_MEM_EQ(actual.data, expected, actual.size);
}

/* Five pairs flatten into twenty core values, so they spill both into the entry point and into the
 * host function it passes them to. The entry point's result, a pair, is in a return area, as the
 * pair the host function returns is, with the strings deep in each. The script returns a string of
 * its own and one of its arguments, which are copied before the arguments are freed. */
TEST(arguments_spill_and_results_cross_in_return_areas) {
    const struct running running = start();
    test_shapes_host_pair result;
    for (int i = 0; i < 2; i++) {
        ASSERT_INT_EQ(test_shapes_user_spill(running.instance, pair_of("a left", "a right"),
                                             pair_of("b left", "b right"),
                                             pair_of("c left", "c right"),
                                             pair_of("d left", "d right"),
                                             pair_of("the left of e", "the right of e"), &result),
                      STASIS_OK);
        expect_string(result.left.text, "the given left");
        expect_string(result.right, "the right of e");
    }
    static const char *const passed[5][2] = {
        {"a left", "a right"},
        {"b left", "b right"},
        {"c left", "c right"},
        {"d left", "d right"},
        {"the left of e", "the right of e"},
    };
    for (size_t i = 0; i < 5; i++) {
        ASSERT_STR_EQ(taken[i][0], passed[i][0]);
        ASSERT_STR_EQ(taken[i][1], passed[i][1]);
    }
    /* 4 pairs of 6 and 7 bytes, and 13 and 14 bytes: 79. */
    ASSERT_STR_EQ(output, "took 0x1.3cp+6\ntook 0x1.3cp+6\n");
    stop(running);
}

/* A record of one value crosses as that value, into an entry point and a host function, and out of
 * both as a record that holds one handle. */
TEST(a_record_of_one_value_crosses_as_that_value) {
    const struct running running = start();
    test_shapes_host_wrapped value;
    value.inner.value = -2.75;
    test_shapes_host_tagged result;
    result.item = NULL;
    ASSERT_INT_EQ(test_shapes_user_wrap(running.instance, value, &result), STASIS_OK);
    ASSERT_DOUBLE_EQ(tagged_value, -2.75);
    ASSERT_TRUE(result.item == (test_shapes_host_item *)(void *)&item);
    stop(running);
}

/* Strings of many pages, which a script that kept them, or copies of them, would grow its memory
 * by on every call. */
static char long_left[100000];
static char long_right[100000];

/* An entry point that returns its argument returns a copy of it, made before its export frees the
 * argument, and its cabi_post_* frees the copy, strings deep in records included, so the script's
 * memory stays as large as the first call made it. */
TEST(an_entry_point_returns_a_copy_of_its_argument) {
    const struct running running = start();
    memset(long_left, 'l', sizeof long_left);
    memset(long_right, 'r', sizeof long_right);
    test_shapes_host_pair value;
    value.left.text.data = long_left;
    value.left.text.size = sizeof long_left;
    value.right.data = long_right;
    value.right.size = sizeof long_right;
    test_shapes_host_pair result;
    ASSERT_INT_EQ(test_shapes_user_echo(running.instance, value, &result), STASIS_OK);
    size_t size = 0;
    ASSERT_INT_EQ(stasis_instance_snapshot_size(running.instance, &size), STASIS_OK);
    for (int i = 0; i < 8; i++) {
        ASSERT_INT_EQ(test_shapes_user_echo(running.instance, value, &result), STASIS_OK);
        ASSERT_SIZE_EQ(result.left.text.size, sizeof long_left);
        ASSERT_MEM_EQ(result.left.text.data, long_left, sizeof long_left);
        ASSERT_SIZE_EQ(result.right.size, sizeof long_right);
        ASSERT_MEM_EQ(result.right.data, long_right, sizeof long_right);
    }
    size_t after = 0;
    ASSERT_INT_EQ(stasis_instance_snapshot_size(running.instance, &after), STASIS_OK);
    ASSERT_SIZE_EQ(after, size);
    stop(running);
}

TEST_LIST(TEST_CASE(arguments_spill_and_results_cross_in_return_areas),
          TEST_CASE(a_record_of_one_value_crosses_as_that_value),
          TEST_CASE(an_entry_point_returns_a_copy_of_its_argument));
