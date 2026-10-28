/* Strings crossing between the host and a script, as the canonical ABI lays them out: a host
 * function's string arguments, read from the script's memory and checked, and its string result,
 * which Stasis puts into the script's memory through its cabi_realloc, as it does an entry point's
 * string arguments. An entry point's string results are copied before its cabi_post_* frees them.
 * While Stasis is inside cabi_realloc or cabi_post_*, the script may call no import. */

#include <stasis.h>

#include "harness.h"
#include "string_hosts.h"
#include "support.h"
#include "test_strings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* An instance of the script strings.wat, started, with its script and library. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

static struct running start_strings(void) {
    forget_taken();
    struct running running;
    running.library = create_library();
    running.script = load_script(running.library, &test_strings_interface, "strings");
    running.instance = start_instance(running.script);
    return running;
}

static void stop(struct running running) {
    destroy_all(running.instance, running.script, running.library);
}

/* `text` as a stasis_string, without its NUL. */
static stasis_string text_of(const char *text) {
    stasis_string string;
    string.data = text;
    string.size = strlen(text);
    return string;
}

/* Writes the bytes of `text`, without its NUL, at `address` in the instance's memory. */
static void put(stasis_instance *instance, uint32_t address, const char *text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        ASSERT_INT_EQ(
            test_strings_user_store_byte(instance, address + (uint32_t)i, (uint8_t)text[i]),
            STASIS_OK);
    }
}

/* A host function's string argument reaches it as the script passed it, pointing into the script's
 * memory: a string one byte further on in memory starts one byte further on. */
TEST(a_host_function_takes_a_string) {
    const struct running running = start_strings();
    put(running.instance, 4096, "hello, world");
    ASSERT_INT_EQ(test_strings_user_pass_at(running.instance, 4096, 5), STASIS_OK);
    ASSERT_UINT_EQ(taken.calls, 1);
    ASSERT_TRUE(taken_is(0, "hello"));
    const char *hello = taken.strings[0].data;
    ASSERT_INT_EQ(test_strings_user_pass_at(running.instance, 4103, 5), STASIS_OK);
    ASSERT_TRUE(taken_is(0, "world"));
    ASSERT_TRUE(taken.strings[0].data == hello + 7);
    stop(running);
}

/* The script's memory is one page, of 65536 bytes, and a string may end where it does. */
TEST(takes_a_string_that_ends_where_memory_does) {
    const struct running running = start_strings();
    put(running.instance, 65531, "edges");
    ASSERT_INT_EQ(test_strings_user_pass_at(running.instance, 65531, 5), STASIS_OK);
    ASSERT_TRUE(taken_is(0, "edges"));
    ASSERT_INT_EQ(test_strings_user_pass_at(running.instance, 65536, 0), STASIS_OK);
    ASSERT_TRUE(taken_is(0, ""));
    stop(running);
}

/* Checks that the last call into `instance` trapped as `kind` with `message`. */
static void expect_trap(stasis_instance *instance, stasis_trap_kind kind, const char *message) {
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, kind);
    ASSERT_STR_EQ(trap->message, message);
}

/* Calls `pass_at` with `address` and `size` in a new instance of strings.wasm, which must trap as a
 * boundary violation with `message`, before the host function runs. */
static void expect_pass_refused(uint32_t address, uint32_t size, const char *message) {
    const struct running running = start_strings();
    ASSERT_INT_EQ(test_strings_user_pass_at(running.instance, address, size), STASIS_TRAP);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION, message);
    stop(running);
}

TEST(a_string_outside_memory_traps) {
    expect_pass_refused(65532, 5,
                        "the script passed argument 1 of `host.take` at 65532 in its memory, where "
                        "its 5 bytes run past its end at 65536");
    expect_pass_refused(0xFFFFFFFFU, 2,
                        "the script passed argument 1 of `host.take` at 4294967295 in its memory, "
                        "where its 2 bytes run past its end at 65536");
    expect_pass_refused(
        16, 0xFFFFFFFFU,
        "the script passed argument 1 of `host.take` at 16 in its memory, where its "
        "4294967295 bytes run past its end at 65536");
    expect_pass_refused(65537, 0,
                        "the script passed argument 1 of `host.take` at 65537 in its memory, where "
                        "its 0 bytes run past its end at 65536");
}

/* UTF-8 at the edges of each length of encoding, and a NUL, which is UTF-8 like any other
 * character. */
static const char *const utf8[] = {
    "\x7f",
    "\xc2\x80",
    "\xdf\xbf",
    "\xe0\xa0\x80",
    "\xed\x9f\xbf",
    "\xee\x80\x80",
    "\xef\xbf\xbf",
    "\xf0\x90\x80\x80",
    "\xf4\x8f\xbf\xbf",
    "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z",
};

TEST(takes_any_utf8) {
    const struct running running = start_strings();
    for (size_t i = 0; i < sizeof utf8 / sizeof utf8[0]; i++) {
        put(running.instance, 4096, utf8[i]);
        ASSERT_INT_EQ(test_strings_user_pass_at(running.instance, 4096, (uint32_t)strlen(utf8[i])),
                      STASIS_OK);
        ASSERT_TRUE(taken_is(0, utf8[i]));
    }
    put(running.instance, 4096, "abc");
    ASSERT_INT_EQ(test_strings_user_store_byte(running.instance, 4097, 0), STASIS_OK);
    ASSERT_INT_EQ(test_strings_user_pass_at(running.instance, 4096, 3), STASIS_OK);
    ASSERT_SIZE_EQ(taken.strings[0].size, 3);
    ASSERT_MEM_EQ(taken.strings[0].bytes, "a\0c", 3);
    stop(running);
}

/* Bytes that aren't UTF-8, each with the offset of the first character that isn't. */
static const struct {
    const char *bytes;
    unsigned offset;
} not_utf8[] = {
    /* A continuation byte with no lead byte. */
    {"abc\x80", 3},
    {"\xbf", 0},
    /* Bytes that never start a character. */
    {"\xc0\x80", 0},
    {"\xc1\xbf", 0},
    {"x\xf5\x80\x80\x80", 1},
    {"\xff", 0},
    /* Encodings longer than their characters need. */
    {"\xe0\x9f\xbf", 0},
    {"\xf0\x8f\xbf\xbf", 0},
    /* Surrogates. */
    {"\xed\xa0\x80", 0},
    {"\xed\xbf\xbf", 0},
    /* Past U+10FFFF. */
    {"\xf4\x90\x80\x80", 0},
    /* A lead byte without its continuation bytes, at the end or before another character. */
    {"ok\xc3", 2},
    {"\xe2\x82", 0},
    {"\xf0\x9f\x98", 0},
    {"\xc3z", 0},
    {"\xe2\x82z", 0},
    {"\xf0\x9f\x98z", 0},
};

TEST(a_string_that_isnt_utf8_traps) {
    for (size_t i = 0; i < sizeof not_utf8 / sizeof not_utf8[0]; i++) {
        const struct running running = start_strings();
        put(running.instance, 4096, not_utf8[i].bytes);
        const size_t size = strlen(not_utf8[i].bytes);
        ASSERT_INT_EQ(test_strings_user_pass_at(running.instance, 4096, (uint32_t)size),
                      STASIS_TRAP);
        ASSERT_UINT_EQ(taken.calls, 0);
        char message[256];
        (void)snprintf(message, sizeof message,
                       "the script passed argument 1 of `host.take` as a string of %u bytes that "
                       "isn't UTF-8, from offset %u",
                       (unsigned)size, not_utf8[i].offset);
        expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION, message);
        stop(running);
    }
}

/* A message counts the host function's arguments, not the core values they flatten into: the
 * second string of `take_mixed` is its fourth argument, though its fifth and sixth core values. */
TEST(names_a_string_argument_by_its_position) {
    const struct running running = start_strings();
    put(running.instance, 4096, "fine");
    ASSERT_INT_EQ(test_strings_user_pass_mixed_at(running.instance, 4096, 4, 65534, 3),
                  STASIS_TRAP);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed argument 4 of `host.take_mixed` at 65534 in its memory, where "
                "its 3 bytes run past its end at 65536");
    stop(running);
    const struct running again = start_strings();
    put(again.instance, 4096, "\xff");
    ASSERT_INT_EQ(test_strings_user_pass_mixed_at(again.instance, 4096, 1, 4096, 0), STASIS_TRAP);
    expect_trap(again.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed argument 2 of `host.take_mixed` as a string of 1 bytes that "
                "isn't UTF-8, from offset 0");
    stop(again);
}

/* Nine strings flatten into eighteen core values, which is more than there can be, so the script
 * passes the address of a tuple of them in its memory, each string as its address and size. */
TEST(reads_spilled_strings_from_the_scripts_memory) {
    const struct running running = start_strings();
    ASSERT_INT_EQ(test_strings_user_pass_nine(running.instance), STASIS_OK);
    ASSERT_UINT_EQ(taken.calls, 1);
    const char *const expected[] = {"1",      "22",      "333",      "4444",     "55555",
                                    "666666", "7777777", "88888888", "999999999"};
    for (size_t i = 0; i < 9; i++) {
        ASSERT_TRUE(taken_is(i, expected[i]));
    }
    ASSERT_UINT_EQ(taken.last, 7);
    stop(running);
}

/* Calls `pass_nine_at` with `address` in a new instance of strings.wasm, once the case has laid out
 * the instance's memory with `lay_out`, which must trap as a boundary violation with `message`,
 * before the host function runs. */
static void expect_nine_refused(void (*lay_out)(stasis_instance *instance), uint32_t address,
                                const char *message) {
    const struct running running = start_strings();
    lay_out(running.instance);
    ASSERT_INT_EQ(test_strings_user_pass_nine_at(running.instance, address), STASIS_TRAP);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION, message);
    stop(running);
}

/* Lays out the nine strings and 7 at 4096, as `pass_nine` does, but with the ninth string past the
 * end of memory. */
static void lay_out_ninth_outside(stasis_instance *instance) {
    for (uint32_t i = 0; i < 9; i++) {
        ASSERT_INT_EQ(test_strings_user_store(instance, 4096 + (8 * i), 1024), STASIS_OK);
        ASSERT_INT_EQ(test_strings_user_store(instance, 4100 + (8 * i), 1), STASIS_OK);
    }
    ASSERT_INT_EQ(test_strings_user_store(instance, 4160, 65535), STASIS_OK);
    ASSERT_INT_EQ(test_strings_user_store(instance, 4164, 2), STASIS_OK);
}

static void lay_out_nothing(stasis_instance *instance) { (void)instance; }

TEST(a_spilled_string_outside_memory_traps) {
    expect_nine_refused(lay_out_ninth_outside, 4096,
                        "the script passed argument 9 of `host.take_nine` at 65535 in its memory, "
                        "where its 2 bytes run past its end at 65536");
}

/* The tuple is as aligned as a string's address and size need. */
TEST(misaligned_spilled_strings_trap) {
    expect_nine_refused(
        lay_out_nothing, 4098,
        "the script passed the arguments of `host.take_nine` at 4098 in its memory, "
        "which isn't a multiple of their alignment, 4");
}

/* Checks the arguments of the last call of the script's cabi_realloc. */
static void expect_realloc(stasis_instance *instance, uint32_t alignment, uint32_t size) {
    const uint32_t expected[] = {0, 0, alignment, size};
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t argument = 0xFFFFFFFFU;
        ASSERT_INT_EQ(test_strings_user_realloc_argument(instance, i, &argument), STASIS_OK);
        ASSERT_UINT_EQ(argument, expected[i]);
    }
}

/* Stasis puts the string into the script's memory, as bytes that it allocates there with the
 * script's cabi_realloc, as the canonical ABI does: with no old allocation, and aligned to 1. */
TEST(an_entry_point_takes_a_string) {
    const struct running running = start_strings();
    uint32_t size = 0;
    ASSERT_INT_EQ(test_strings_user_measure(running.instance, text_of("hello"), &size), STASIS_OK);
    ASSERT_UINT_EQ(size, 5);
    uint32_t reallocs = 0;
    ASSERT_INT_EQ(test_strings_user_reallocs(running.instance, &reallocs), STASIS_OK);
    ASSERT_UINT_EQ(reallocs, 1);
    expect_realloc(running.instance, 1, 5);
    stop(running);
}

/* The string crosses to the script, and from the script to the host function, as it was. */
TEST(a_string_crosses_to_the_script_and_back) {
    const struct running running = start_strings();
    ASSERT_INT_EQ(test_strings_user_pass(running.instance, text_of("h\xc3\xa9llo, w\xc3\xb6rld")),
                  STASIS_OK);
    ASSERT_UINT_EQ(taken.calls, 1);
    ASSERT_TRUE(taken_is(0, "h\xc3\xa9llo, w\xc3\xb6rld"));
    stop(running);
}

/* An empty string is allocated too, as 0 bytes, and may have null data. */
TEST(an_empty_string_crosses) {
    const struct running running = start_strings();
    stasis_string empty;
    empty.data = NULL;
    empty.size = 0;
    ASSERT_INT_EQ(test_strings_user_pass(running.instance, empty), STASIS_OK);
    ASSERT_TRUE(taken_is(0, ""));
    expect_realloc(running.instance, 1, 0);
    ASSERT_INT_EQ(test_strings_user_pass(running.instance, text_of("")), STASIS_OK);
    ASSERT_TRUE(taken_is(0, ""));
    stop(running);
}

/* Strings among scalars each flatten into two core values, their address and size, in place. */
TEST(strings_cross_among_scalars) {
    const struct running running = start_strings();
    ASSERT_INT_EQ(test_strings_user_pass_mixed(running.instance, 200, text_of("first"),
                                               0x123456789ABCDEF0U, text_of("second"), -2.5F),
                  STASIS_OK);
    ASSERT_UINT_EQ(taken.calls, 1);
    ASSERT_UINT_EQ(taken.before, 200);
    ASSERT_TRUE(taken_is(0, "first"));
    ASSERT_UINT_EQ(taken.middle, 0x123456789ABCDEF0U);
    ASSERT_TRUE(taken_is(1, "second"));
    ASSERT_FLOAT_EQ(taken.after, -2.5F);
    stop(running);
}

/* A string the host passes must have its data unless it's empty, be no larger than 2^31 - 1 bytes,
 * as the canonical ABI has it, and be UTF-8. One that isn't is refused before the script runs, and
 * the instance is as it was. */
TEST(refuses_a_string_argument_that_cant_cross) {
    const struct running running = start_strings();
    stasis_string strings[3];
    strings[0].data = NULL;
    strings[0].size = 3;
    strings[1] = text_of("caf\xe9");
    strings[2].data = "x";
    strings[2].size = 0x80000000U;
    for (size_t i = 0; i < 3; i++) {
        uint32_t size = 99;
        ASSERT_INT_EQ(test_strings_user_measure(running.instance, strings[i], &size),
                      STASIS_INVALID_ARGUMENT);
        ASSERT_UINT_EQ(size, 0);
    }
    uint32_t reallocs = 99;
    ASSERT_INT_EQ(test_strings_user_reallocs(running.instance, &reallocs), STASIS_OK);
    ASSERT_UINT_EQ(reallocs, 0);
    stop(running);
}

/* Starts a new instance of strings.wasm whose cabi_realloc does as `mode` says, as strings.wat
 * explains each mode. */
static struct running start_reallocating(uint32_t mode) {
    const struct running running = start_strings();
    ASSERT_INT_EQ(test_strings_user_set_realloc(running.instance, mode), STASIS_OK);
    return running;
}

/* The index of `host.give` among the functions of strings.wasm, whose imports come first. */
enum { GIVE_IMPORT = 3 };

/* The modes of strings.wat's cabi_realloc. */
enum {
    REALLOC_NULL = 1,
    REALLOC_PAST_THE_END = 2,
    REALLOC_TRAPS = 3,
    REALLOC_CALLS_THE_HOST = 4,
    REALLOC_GROWS = 5,
};

/* Calls `measure` with "hello" in a new instance whose cabi_realloc does as `mode` says, which must
 * trap as `kind` with `message`, before the entry point runs. */
static void expect_measure_traps(uint32_t mode, stasis_trap_kind kind, const char *message) {
    const struct running running = start_reallocating(mode);
    uint32_t size = 99;
    ASSERT_INT_EQ(test_strings_user_measure(running.instance, text_of("hello"), &size),
                  STASIS_TRAP);
    ASSERT_UINT_EQ(size, 0);
    expect_trap(running.instance, kind, message);
    stop(running);
}

TEST(a_null_from_cabi_realloc_traps) {
    expect_measure_traps(REALLOC_NULL, STASIS_TRAP_KIND_ALLOCATION_FAILED,
                         "the script's `cabi_realloc` returned null for the 5 bytes of argument 1 "
                         "of `user.measure`");
}

/* No bytes need no memory, so cabi_realloc may answer a request for 0 of them with null, as the
 * canonical ABI allows. */
TEST(cabi_realloc_may_return_null_for_no_bytes) {
    const struct running running = start_reallocating(REALLOC_NULL);
    ASSERT_INT_EQ(test_strings_user_pass(running.instance, text_of("")), STASIS_OK);
    ASSERT_TRUE(taken_is(0, ""));
    expect_realloc(running.instance, 1, 0);
    stop(running);
}

TEST(an_allocation_outside_memory_traps) {
    expect_measure_traps(REALLOC_PAST_THE_END, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                         "the script's `cabi_realloc` returned 65532 for the 5 bytes of argument 1 "
                         "of `user.measure`, where they run past the end of its memory at 65536");
}

/* Stasis's own call into cabi_realloc traps the call into the instance, with its kind, its message
 * and its frames, which are cabi_realloc's alone, since the entry point never ran. */
TEST(a_trap_in_cabi_realloc_traps_the_call) {
    expect_measure_traps(REALLOC_TRAPS, STASIS_TRAP_KIND_UNREACHABLE, "unreachable");
    const struct running running = start_reallocating(REALLOC_TRAPS);
    ASSERT_INT_EQ(test_strings_user_pass(running.instance, text_of("hello")), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(running.instance);
    ASSERT_SIZE_EQ(trap->frame_count, 1);
    ASSERT_STR_EQ(trap->frames[0].function_name, "cabi_realloc");
    ASSERT_UINT_EQ(taken.calls, 0);
    stop(running);
}

/* Whether `string` is `expected`, a NUL-terminated string. */
static bool string_is(stasis_string string, const char *expected) {
    const size_t size = strlen(expected);
    if (string.size != size) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    return memcmp(string.data, expected, size) == 0;
}

/* Checks how many times the script's cabi_post_* functions have been called, and the argument of
 * the last call that had one. */
static void expect_posts(stasis_instance *instance, uint32_t count, uint32_t argument) {
    uint32_t posts = 0;
    ASSERT_INT_EQ(test_strings_user_posts(instance, &posts), STASIS_OK);
    ASSERT_UINT_EQ(posts, count);
    uint32_t last = 0;
    ASSERT_INT_EQ(test_strings_user_post_argument(instance, &last), STASIS_OK);
    ASSERT_UINT_EQ(last, argument);
}

/* The script returns a string as the address of a return area, at 64, that holds the string's
 * address and size. Stasis copies the string before it calls the entry point's cabi_post_*, with
 * that address, which overwrites it in the script's memory. */
TEST(an_entry_point_returns_a_string) {
    const struct running running = start_strings();
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_echo(running.instance, text_of("hello"), &result), STASIS_OK);
    ASSERT_TRUE(string_is(result, "hello"));
    expect_posts(running.instance, 1, 64);
    stop(running);
}

/* The result lasts until the next call into its instance, whatever happens in other instances. */
TEST(a_string_result_lasts_until_the_next_call) {
    const struct running first = start_strings();
    const struct running second = start_strings();
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_echo(first.instance, text_of("first"), &result), STASIS_OK);
    stasis_string other = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_echo(second.instance, text_of("second"), &other), STASIS_OK);
    ASSERT_TRUE(string_is(result, "first"));
    ASSERT_TRUE(string_is(other, "second"));
    stop(second);
    ASSERT_TRUE(string_is(result, "first"));
    stop(first);
}

TEST(an_entry_point_returns_an_empty_string) {
    const struct running running = start_strings();
    stasis_string result = text_of("not returned");
    ASSERT_INT_EQ(test_strings_user_echo(running.instance, text_of(""), &result), STASIS_OK);
    ASSERT_SIZE_EQ(result.size, 0);
    stop(running);
}

/* An entry point's cabi_post_* takes its core results, whatever they are: a u32 here, and nothing
 * for an entry point that returns nothing. */
TEST(cabi_post_takes_the_core_results) {
    const struct running running = start_strings();
    uint32_t size = 0;
    ASSERT_INT_EQ(test_strings_user_measure(running.instance, text_of("seven b"), &size),
                  STASIS_OK);
    expect_posts(running.instance, 1, 7);
    ASSERT_INT_EQ(test_strings_user_pass(running.instance, text_of("x")), STASIS_OK);
    expect_posts(running.instance, 2, 7);
    stop(running);
}

/* Lays out a return area at `area` for the string of `size` bytes at `data`. */
static void lay_out_area(stasis_instance *instance, uint32_t area, uint32_t data, uint32_t size) {
    ASSERT_INT_EQ(test_strings_user_store(instance, area, data), STASIS_OK);
    ASSERT_INT_EQ(test_strings_user_store(instance, area + 4, size), STASIS_OK);
}

/* A return area may be anywhere in memory that's aligned, and its string may end where memory
 * does. */
TEST(returns_a_string_from_any_return_area) {
    const struct running running = start_strings();
    put(running.instance, 65531, "edges");
    lay_out_area(running.instance, 4096, 65531, 5);
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_return_at(running.instance, 4096, &result), STASIS_OK);
    ASSERT_TRUE(string_is(result, "edges"));
    put(running.instance, 5000, "middle");
    lay_out_area(running.instance, 65528, 5000, 6);
    ASSERT_INT_EQ(test_strings_user_return_at(running.instance, 65528, &result), STASIS_OK);
    ASSERT_TRUE(string_is(result, "middle"));
    stop(running);
}

/* Calls `return_at` with `area` in an instance that `lay_out` has laid out, which must trap as a
 * boundary violation with `message`, leaving the result zeroed. */
static void expect_return_refused(void (*lay_out)(stasis_instance *instance), uint32_t area,
                                  const char *message) {
    const struct running running = start_strings();
    lay_out(running.instance);
    stasis_string result = text_of("not returned");
    ASSERT_INT_EQ(test_strings_user_return_at(running.instance, area, &result), STASIS_TRAP);
    ASSERT_TRUE(result.data == NULL);
    ASSERT_SIZE_EQ(result.size, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION, message);
    stop(running);
}

/* Lays out return areas at 4096 for a string past the end of memory, and at 4104 for one that isn't
 * UTF-8. */
static void lay_out_bad_results(stasis_instance *instance) {
    lay_out_area(instance, 4096, 65534, 3);
    put(instance, 5000, "ab\xc0z");
    lay_out_area(instance, 4104, 5000, 4);
}

TEST(a_return_area_outside_memory_traps) {
    expect_return_refused(lay_out_nothing, 65532,
                          "the entry point `user.return_at` returned its result at 65532 in the "
                          "script's memory, where its 8 bytes run past its end at 65536");
    expect_return_refused(lay_out_nothing, 0xFFFFFFFCU,
                          "the entry point `user.return_at` returned its result at 4294967292 in "
                          "the script's memory, where its 8 bytes run past its end at 65536");
}

TEST(a_misaligned_return_area_traps) {
    expect_return_refused(lay_out_nothing, 4098,
                          "the entry point `user.return_at` returned its result at 4098 in the "
                          "script's memory, which isn't a multiple of its alignment, 4");
}

TEST(a_string_result_outside_memory_traps) {
    expect_return_refused(lay_out_bad_results, 4096,
                          "the entry point `user.return_at` returned a string at 65534 in the "
                          "script's memory, where its 3 bytes run past its end at 65536");
}

TEST(a_string_result_that_isnt_utf8_traps) {
    expect_return_refused(lay_out_bad_results, 4104,
                          "the entry point `user.return_at` returned a string of 4 bytes that "
                          "isn't UTF-8, from offset 2");
}

/* How many times `host.give` and `host.swap` have run since the case set it to 0. */
static unsigned gives;

static void count_give(stasis_instance *instance) {
    (void)instance;
    gives++;
}

/* Starts a new instance of strings.wasm, with `host.give` and `host.swap` replying `text` and
 * counting their calls. */
static struct running start_giving(const char *text) {
    const struct running running = start_strings();
    reply = text_of(text);
    on_give = count_give;
    gives = 0;
    return running;
}

static void stop_giving(struct running running) {
    on_give = NULL;
    stop(running);
}

/* Stasis puts the host function's string into the script's memory with the script's cabi_realloc,
 * and its address and size into the return area the script passed, at 72, which the entry point
 * then returns. */
TEST(a_host_function_returns_a_string) {
    const struct running running = start_giving("from the h\xc3\xb6st");
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_fetch(running.instance, &result), STASIS_OK);
    ASSERT_TRUE(string_is(result, "from the h\xc3\xb6st"));
    ASSERT_UINT_EQ(gives, 1);
    expect_realloc(running.instance, 1, 14);
    expect_posts(running.instance, 1, 72);
    stop_giving(running);
}

/* A host function can take a string and return one. */
TEST(a_host_function_takes_and_returns_strings) {
    const struct running running = start_giving("reply");
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_relay(running.instance, text_of("request"), &result),
                  STASIS_OK);
    ASSERT_TRUE(taken_is(0, "request"));
    ASSERT_TRUE(string_is(result, "reply"));
    stop_giving(running);
}

TEST(a_host_function_returns_an_empty_string) {
    const struct running running = start_giving("");
    reply.data = NULL;
    stasis_string result = text_of("not returned");
    ASSERT_INT_EQ(test_strings_user_fetch(running.instance, &result), STASIS_OK);
    ASSERT_SIZE_EQ(result.size, 0);
    expect_realloc(running.instance, 1, 0);
    stop_giving(running);
}

/* Calls `fetch_into` with `area`, which must trap as a boundary violation with `message`, before
 * `host.give` runs. */
static void expect_area_refused(uint32_t area, const char *message) {
    const struct running running = start_giving("never given");
    ASSERT_INT_EQ(test_strings_user_fetch_into(running.instance, area), STASIS_TRAP);
    ASSERT_UINT_EQ(gives, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION, message);
    stop_giving(running);
}

TEST(a_return_area_passed_outside_memory_traps) {
    expect_area_refused(65532, "the script passed the return area of `host.give` at 65532 in its "
                               "memory, where its 8 bytes run past its end at 65536");
}

TEST(a_misaligned_return_area_passed_traps) {
    expect_area_refused(4097, "the script passed the return area of `host.give` at 4097 in its "
                              "memory, which isn't a multiple of its alignment, 4");
}

/* Calls `fetch` with `host.give` replying `text`, which must trap as a host abort with `message`,
 * once the host function has run. */
static void expect_reply_refused(stasis_string text, const char *message) {
    const struct running running = start_giving("");
    reply = text;
    stasis_string result = text_of("not returned");
    ASSERT_INT_EQ(test_strings_user_fetch(running.instance, &result), STASIS_TRAP);
    ASSERT_UINT_EQ(gives, 1);
    ASSERT_SIZE_EQ(result.size, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_HOST_ABORT, message);
    stop_giving(running);
}

/* A host function's string must have its data unless it's empty, be no larger than a string may
 * be, and be UTF-8, or the call traps as the host's fault. */
TEST(a_host_function_returning_a_string_that_cant_cross_traps) {
    stasis_string text;
    text.data = NULL;
    text.size = 3;
    expect_reply_refused(text, "the host function `host.give` returned a string of 3 bytes whose "
                               "data is null");
    text.data = "x";
    text.size = 0x80000000U;
    expect_reply_refused(text, "the host function `host.give` returned a string of 2147483648 "
                               "bytes, more than the 2147483647 a string may have");
    expect_reply_refused(text_of("ok\xed\xa0\x80"), "the host function `host.give` returned a "
                                                    "string of 5 bytes that isn't UTF-8, from "
                                                    "offset 2");
}

/* Calls `fetch` in a new instance whose cabi_realloc does as `mode` says, which must trap as `kind`
 * with `message`, with the host function's frame innermost, and then the entry point's. */
static void expect_fetch_traps(uint32_t mode, stasis_trap_kind kind, const char *message) {
    const struct running running = start_giving("given");
    ASSERT_INT_EQ(test_strings_user_set_realloc(running.instance, mode), STASIS_OK);
    stasis_string result = text_of("not returned");
    ASSERT_INT_EQ(test_strings_user_fetch(running.instance, &result), STASIS_TRAP);
    ASSERT_SIZE_EQ(result.size, 0);
    expect_trap(running.instance, kind, message);
    const stasis_trap *trap = stasis_instance_trap(running.instance);
    ASSERT_SIZE_EQ(trap->frame_count, 2);
    ASSERT_UINT_EQ(trap->frames[0].function_index, GIVE_IMPORT);
    ASSERT_STR_EQ(trap->frames[1].function_name, "fetch");
    stop_giving(running);
}

TEST(a_null_from_cabi_realloc_for_a_result_traps) {
    expect_fetch_traps(REALLOC_NULL, STASIS_TRAP_KIND_ALLOCATION_FAILED,
                       "the script's `cabi_realloc` returned null for the 5 bytes of the result of "
                       "`host.give`");
}

TEST(an_allocation_for_a_result_outside_memory_traps) {
    expect_fetch_traps(REALLOC_PAST_THE_END, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                       "the script's `cabi_realloc` returned 65532 for the 5 bytes of the result "
                       "of `host.give`, where they run past the end of its memory at 65536");
}

/* A trap in the script's cabi_realloc, which Stasis calls from inside the host function's call,
 * traps the call into the instance with its own kind, message and frames: cabi_realloc's, inside
 * the host function's, inside the entry point's. */
TEST(a_trap_in_cabi_realloc_for_a_result_traps_the_call) {
    const struct running running = start_giving("given");
    ASSERT_INT_EQ(test_strings_user_set_realloc(running.instance, REALLOC_TRAPS), STASIS_OK);
    stasis_string result = text_of("not returned");
    ASSERT_INT_EQ(test_strings_user_fetch(running.instance, &result), STASIS_TRAP);
    ASSERT_SIZE_EQ(result.size, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_UNREACHABLE, "unreachable");
    const stasis_trap *trap = stasis_instance_trap(running.instance);
    ASSERT_SIZE_EQ(trap->frame_count, 3);
    ASSERT_STR_EQ(trap->frames[0].function_name, "cabi_realloc");
    ASSERT_UINT_EQ(trap->frames[1].function_index, GIVE_IMPORT);
    ASSERT_STR_EQ(trap->frames[2].function_name, "fetch");
    ASSERT_UINT_EQ(gives, 1);
    stop_giving(running);
}

/* The modes of strings.wat's cabi_post_* functions. */
enum {
    POST_CALLS_THE_HOST = 1,
    POST_TRAPS = 2,
    POST_GROWS = 3,
};

/* A trap in an entry point's cabi_post_* traps the call, and the result is lost. */
TEST(a_trap_in_cabi_post_traps_the_call) {
    const struct running running = start_strings();
    ASSERT_INT_EQ(test_strings_user_set_post(running.instance, POST_TRAPS), STASIS_OK);
    stasis_string result = text_of("not returned");
    ASSERT_INT_EQ(test_strings_user_echo(running.instance, text_of("lost"), &result), STASIS_TRAP);
    ASSERT_TRUE(result.data == NULL);
    ASSERT_SIZE_EQ(result.size, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_UNREACHABLE, "unreachable");
    ASSERT_STR_EQ(stasis_instance_trap(running.instance)->frames[0].function_name, "post");
    stop(running);
}

/* The index of `host.take` among the functions of strings.wasm. */
enum { TAKE_IMPORT = 0 };

/* While Stasis is inside the script's cabi_realloc, the script may call no import, so the call
 * traps before the host function runs. */
TEST(cabi_realloc_may_call_no_import) {
    const struct running running = start_reallocating(REALLOC_CALLS_THE_HOST);
    ASSERT_INT_EQ(test_strings_user_pass(running.instance, text_of("hello")), STASIS_TRAP);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script called `host.take` from its `cabi_realloc`, where it may call no "
                "import");
    const stasis_trap *trap = stasis_instance_trap(running.instance);
    ASSERT_SIZE_EQ(trap->frame_count, 2);
    ASSERT_UINT_EQ(trap->frames[0].function_index, TAKE_IMPORT);
    ASSERT_STR_EQ(trap->frames[1].function_name, "cabi_realloc");
    stop(running);
}

/* So a script can't recurse through the host: a cabi_realloc that calls the host as Stasis lowers a
 * host function's result traps at once, one level deep, and the first trap's message is the
 * report's. */
TEST(a_cabi_realloc_that_calls_the_host_traps_at_depth_1) {
    const struct running running = start_giving("given");
    ASSERT_INT_EQ(test_strings_user_set_realloc(running.instance, REALLOC_CALLS_THE_HOST),
                  STASIS_OK);
    stasis_string result = text_of("not returned");
    ASSERT_INT_EQ(test_strings_user_fetch(running.instance, &result), STASIS_TRAP);
    ASSERT_UINT_EQ(gives, 1);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script called `host.take` from its `cabi_realloc`, where it may call no "
                "import");
    const stasis_trap *trap = stasis_instance_trap(running.instance);
    ASSERT_SIZE_EQ(trap->frame_count, 4);
    ASSERT_UINT_EQ(trap->frames[0].function_index, TAKE_IMPORT);
    ASSERT_STR_EQ(trap->frames[1].function_name, "cabi_realloc");
    ASSERT_UINT_EQ(trap->frames[2].function_index, GIVE_IMPORT);
    ASSERT_STR_EQ(trap->frames[3].function_name, "fetch");
    stop_giving(running);
}

TEST(cabi_post_may_call_no_import) {
    const struct running running = start_strings();
    ASSERT_INT_EQ(test_strings_user_set_post(running.instance, POST_CALLS_THE_HOST), STASIS_OK);
    stasis_string result = text_of("not returned");
    ASSERT_INT_EQ(test_strings_user_echo(running.instance, text_of("lost"), &result), STASIS_TRAP);
    ASSERT_SIZE_EQ(result.size, 0);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script called `host.take` from its `cabi_post_*` for `user.echo`, where it "
                "may call no import");
    stop(running);
}

/* Once Stasis is out of cabi_realloc and cabi_post_*, the script may call imports again. */
TEST(the_script_may_call_imports_again_afterwards) {
    const struct running running = start_giving("given");
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_relay(running.instance, text_of("first"), &result), STASIS_OK);
    ASSERT_INT_EQ(test_strings_user_relay(running.instance, text_of("second"), &result), STASIS_OK);
    ASSERT_TRUE(taken_is(0, "second"));
    ASSERT_UINT_EQ(taken.calls, 2);
    stop_giving(running);
}

/* Checks the number of pages of the instance's memory. */
static void expect_pages(stasis_instance *instance, uint32_t expected) {
    uint32_t pages = 0;
    ASSERT_INT_EQ(test_strings_user_pages(instance, &pages), STASIS_OK);
    ASSERT_UINT_EQ(pages, expected);
}

/* A string of `size` bytes, each a letter of the alphabet in turn from `first`, in `buffer`. */
static stasis_string letters(char *buffer, size_t size, char first) {
    for (size_t i = 0; i < size; i++) {
        buffer[i] = (char)('a' + ((first - 'a' + (int)(i % 26)) % 26));
    }
    stasis_string string;
    string.data = buffer;
    string.size = size;
    return string;
}

/* Whether the string at `index` of those taken is `expected`. */
static bool taken_equals(size_t index, stasis_string expected) {
    if (index >= taken.count || taken.strings[index].size != expected.size) {
        return false;
    }
    if (expected.size == 0) {
        return true;
    }
    return memcmp(taken.strings[index].bytes, expected.data, expected.size) == 0;
}

/* Strings large enough to cross pages, which the tests that grow memory pass. */
enum { LARGE = 100000 };

static char large_first[LARGE];
static char large_second[LARGE];

/* cabi_realloc grows memory each time Stasis calls it, which can move it. So Stasis takes where the
 * memory is afresh after each call, for each argument it puts there. */
TEST(arguments_cross_when_cabi_realloc_grows_memory) {
    const struct running running = start_reallocating(REALLOC_GROWS);
    const stasis_string first = letters(large_first, LARGE, 'a');
    const stasis_string second = letters(large_second, LARGE, 'n');
    ASSERT_INT_EQ(test_strings_user_pass_mixed(running.instance, 1, first, 2, second, 3.0F),
                  STASIS_OK);
    ASSERT_TRUE(taken_equals(0, first));
    ASSERT_TRUE(taken_equals(1, second));
    expect_pages(running.instance, 1 + 16 + 16);
    stop(running);
}

/* So too for a host function's result, whose return area Stasis writes into once cabi_realloc has
 * grown memory. */
TEST(a_host_functions_result_crosses_when_cabi_realloc_grows_memory) {
    const struct running running = start_giving("");
    reply = letters(large_first, LARGE, 'g');
    ASSERT_INT_EQ(test_strings_user_set_realloc(running.instance, REALLOC_GROWS), STASIS_OK);
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(
        test_strings_user_relay(running.instance, letters(large_second, LARGE, 'r'), &result),
        STASIS_OK);
    ASSERT_TRUE(taken_equals(0, letters(large_second, LARGE, 'r')));
    ASSERT_SIZE_EQ(result.size, LARGE);
    ASSERT_MEM_EQ(result.data, large_first, LARGE);
    expect_pages(running.instance, 1 + 16 + 16);
    stop_giving(running);
}

/* An entry point's string result is copied before its cabi_post_* runs, so it lasts even when that
 * grows the memory. */
TEST(a_result_lasts_when_cabi_post_grows_memory) {
    const struct running running = start_strings();
    ASSERT_INT_EQ(test_strings_user_set_post(running.instance, POST_GROWS), STASIS_OK);
    const stasis_string text = letters(large_first, LARGE, 'p');
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_echo(running.instance, text, &result), STASIS_OK);
    expect_pages(running.instance, 2 + 16);
    ASSERT_SIZE_EQ(result.size, LARGE);
    ASSERT_MEM_EQ(result.data, large_first, LARGE);
    stop(running);
}

/* Loads the script <name>.wasm against strings.stasis, which must fail with `status` and the
 * message `expected`, leaving no script behind. */
static void expect_refused(const char *name, stasis_status status, const char *expected) {
    stasis_library *library = create_library();
    const struct module_bytes module = read_module(name);
    char message[2048] = "not written";
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, &test_strings_interface, module.bytes, module.size,
                                     message, sizeof message, &script),
                  status);
    ASSERT_STR_EQ(message, expected);
    ASSERT_TRUE(script == NULL);
    free_module(module);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* A script needs cabi_realloc when Stasis puts values into its memory: the result of a host
 * function it imports that returns a string, or the arguments of an entry point it provides that
 * takes one. */
TEST(refuses_a_script_that_lacks_cabi_realloc_where_its_needed) {
    expect_refused("realloc_missing_for_a_result", STASIS_INVALID_SCRIPT,
                   "the script lacks the export `cabi_realloc`, which Stasis needs to put the "
                   "result of the host function `host.give` into its memory");
    expect_refused("realloc_missing_for_an_argument", STASIS_INVALID_SCRIPT,
                   "the script lacks the export `cabi_realloc`, which Stasis needs to put the "
                   "arguments of the entry point `user.echo` into its memory");
    expect_refused("realloc_not_a_function", STASIS_INVALID_SCRIPT,
                   "the script's `cabi_realloc` is exported as something other than a function");
}

/* cabi_realloc and each cabi_post_* that names one of the script's entry points must be functions
 * of the types the canonical ABI gives them, whether Stasis needs them or not. */
TEST(refuses_abi_exports_of_other_types) {
    expect_refused("abi_exports_wrong_types", STASIS_INVALID_SCRIPT,
                   "the script's `cabi_realloc` is a function of type (i32) -> (i32), where "
                   "Stasis calls it as (i32, i32, i32, i32) -> (i32)\n"
                   "the script's `cabi_post_test:strings/user@1#fetch:ae6637fc59233091` is a "
                   "function of type () -> (), where the results of the entry point `user.fetch` "
                   "make it (i32) -> ()\n"
                   "the script's `cabi_post_test:strings/user@1#pages:5b69886a8b9e31a8` is "
                   "exported as something other than a function");
}

/* Taking a host function's string argument, or an entry point's string result, needs nothing of
 * the script's, which already holds them in its memory. */
TEST(loads_a_script_that_never_needs_cabi_realloc) {
    const struct running running = {create_library(), NULL, NULL};
    stasis_script *script =
        load_script(running.library, &test_strings_interface, "realloc_not_needed");
    stasis_instance *instance = start_instance(script);
    forget_taken();
    ASSERT_INT_EQ(test_strings_user_pass_at(instance, 2048, 5), STASIS_OK);
    ASSERT_TRUE(taken_is(0, "plain"));
    stasis_string result = {NULL, 0};
    ASSERT_INT_EQ(test_strings_user_return_at(instance, 1024, &result), STASIS_OK);
    ASSERT_TRUE(string_is(result, "plain"));
    destroy_all(instance, script, running.library);
}

/* A C script built as a script author builds one, whose cabi_realloc is the C library's realloc,
 * and whose cabi_post_* functions free what Stasis allocated, takes and returns strings of every
 * size over many calls, reusing what it frees. */
TEST(a_c_script_takes_and_returns_strings) {
    const struct running running = {create_library(), NULL, NULL};
    stasis_script *script = load_script(running.library, &test_strings_interface, "c_strings");
    stasis_instance *instance = start_instance(script);
    forget_taken();
    for (size_t round = 0; round < 200; round++) {
        const size_t size = round * 997 % LARGE;
        const stasis_string text = letters(large_first, size, (char)('a' + (round % 26)));
        stasis_string result = {NULL, 0};
        ASSERT_INT_EQ(test_strings_user_echo(instance, text, &result), STASIS_OK);
        ASSERT_SIZE_EQ(result.size, size);
        ASSERT_MEM_EQ(result.data, text.data, size);
        ASSERT_INT_EQ(test_strings_user_pass(instance, text), STASIS_OK);
        ASSERT_TRUE(taken_equals(0, text));
        reply = letters(large_second, LARGE - size, (char)('z' - (round % 26)));
        ASSERT_INT_EQ(test_strings_user_relay(instance, text, &result), STASIS_OK);
        ASSERT_TRUE(taken_equals(0, text));
        ASSERT_SIZE_EQ(result.size, reply.size);
        ASSERT_MEM_EQ(result.data, reply.data, reply.size);
        ASSERT_INT_EQ(test_strings_user_fetch(instance, &result), STASIS_OK);
        ASSERT_SIZE_EQ(result.size, reply.size);
        ASSERT_MEM_EQ(result.data, reply.data, reply.size);
    }
    forget_taken();
    destroy_all(instance, script, running.library);
}

TEST_LIST(
    TEST_CASE(a_host_function_takes_a_string),
    TEST_CASE(takes_a_string_that_ends_where_memory_does), TEST_CASE(a_string_outside_memory_traps),
    TEST_CASE(names_a_string_argument_by_its_position), TEST_CASE(takes_any_utf8),
    TEST_CASE(a_string_that_isnt_utf8_traps),
    TEST_CASE(reads_spilled_strings_from_the_scripts_memory),
    TEST_CASE(a_spilled_string_outside_memory_traps), TEST_CASE(misaligned_spilled_strings_trap),
    TEST_CASE(an_entry_point_takes_a_string), TEST_CASE(a_string_crosses_to_the_script_and_back),
    TEST_CASE(an_empty_string_crosses), TEST_CASE(strings_cross_among_scalars),
    TEST_CASE(refuses_a_string_argument_that_cant_cross), TEST_CASE(a_null_from_cabi_realloc_traps),
    TEST_CASE(cabi_realloc_may_return_null_for_no_bytes),
    TEST_CASE(an_allocation_outside_memory_traps), TEST_CASE(a_trap_in_cabi_realloc_traps_the_call),
    TEST_CASE(an_entry_point_returns_a_string),
    TEST_CASE(a_string_result_lasts_until_the_next_call),
    TEST_CASE(an_entry_point_returns_an_empty_string), TEST_CASE(cabi_post_takes_the_core_results),
    TEST_CASE(returns_a_string_from_any_return_area), TEST_CASE(a_return_area_outside_memory_traps),
    TEST_CASE(a_misaligned_return_area_traps), TEST_CASE(a_string_result_outside_memory_traps),
    TEST_CASE(a_string_result_that_isnt_utf8_traps), TEST_CASE(a_host_function_returns_a_string),
    TEST_CASE(a_host_function_takes_and_returns_strings),
    TEST_CASE(a_host_function_returns_an_empty_string),
    TEST_CASE(a_return_area_passed_outside_memory_traps),
    TEST_CASE(a_misaligned_return_area_passed_traps),
    TEST_CASE(a_host_function_returning_a_string_that_cant_cross_traps),
    TEST_CASE(a_null_from_cabi_realloc_for_a_result_traps),
    TEST_CASE(an_allocation_for_a_result_outside_memory_traps),
    TEST_CASE(a_trap_in_cabi_realloc_for_a_result_traps_the_call),
    TEST_CASE(a_trap_in_cabi_post_traps_the_call), TEST_CASE(cabi_realloc_may_call_no_import),
    TEST_CASE(a_cabi_realloc_that_calls_the_host_traps_at_depth_1),
    TEST_CASE(cabi_post_may_call_no_import),
    TEST_CASE(the_script_may_call_imports_again_afterwards),
    TEST_CASE(arguments_cross_when_cabi_realloc_grows_memory),
    TEST_CASE(a_host_functions_result_crosses_when_cabi_realloc_grows_memory),
    TEST_CASE(a_result_lasts_when_cabi_post_grows_memory),
    TEST_CASE(refuses_a_script_that_lacks_cabi_realloc_where_its_needed),
    TEST_CASE(refuses_abi_exports_of_other_types),
    TEST_CASE(loads_a_script_that_never_needs_cabi_realloc),
    TEST_CASE(a_c_script_takes_and_returns_strings));
