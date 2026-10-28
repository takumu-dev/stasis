#include "string_hosts.h"

#include <stasis.h>

#include "harness.h"
#include "test_strings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct taken taken;
stasis_string reply;
void (*on_give)(stasis_instance *instance);

void forget_taken(void) {
    for (size_t i = 0; i < MOST_TAKEN; i++) {
        free(taken.strings[i].bytes);
    }
    memset(&taken, 0, sizeof taken);
}

bool taken_is(size_t index, const char *expected) {
    const size_t size = strlen(expected);
    if (index >= taken.count || taken.strings[index].size != size) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    return memcmp(taken.strings[index].bytes, expected, size) == 0;
}

/* Starts recording a call that takes `count` strings, forgetting the last. */
static void start_call(size_t count) {
    const unsigned calls = taken.calls;
    forget_taken();
    taken.calls = calls + 1;
    taken.count = count;
}

/* Records the string at `index`. */
static void take_string(size_t index, stasis_string text) {
    taken.strings[index].data = text.data;
    taken.strings[index].size = text.size;
    if (text.size > 0) {
        taken.strings[index].bytes = malloc(text.size);
        ASSERT_TRUE(taken.strings[index].bytes != NULL);
        memcpy(taken.strings[index].bytes, text.data, text.size);
    }
}

void test_strings_host_take(stasis_instance *instance, stasis_string text) {
    (void)instance;
    start_call(1);
    take_string(0, text);
}

void test_strings_host_take_mixed(stasis_instance *instance, uint8_t before, stasis_string first,
                                  uint64_t middle, stasis_string second, float after) {
    (void)instance;
    start_call(2);
    take_string(0, first);
    take_string(1, second);
    taken.before = before;
    taken.middle = middle;
    taken.after = after;
}

void test_strings_host_take_nine(stasis_instance *instance, stasis_string a, stasis_string b,
                                 stasis_string c, stasis_string d, stasis_string e, stasis_string f,
                                 stasis_string g, stasis_string h, stasis_string i, uint8_t j) {
    (void)instance;
    start_call(9);
    const stasis_string strings[MOST_TAKEN] = {a, b, c, d, e, f, g, h, i};
    for (size_t index = 0; index < MOST_TAKEN; index++) {
        take_string(index, strings[index]);
    }
    taken.last = j;
}

stasis_string test_strings_host_give(stasis_instance *instance) {
    if (on_give != NULL) {
        on_give(instance);
    }
    return reply;
}

stasis_string test_strings_host_swap(stasis_instance *instance, stasis_string text) {
    start_call(1);
    take_string(0, text);
    if (on_give != NULL) {
        on_give(instance);
    }
    return reply;
}
