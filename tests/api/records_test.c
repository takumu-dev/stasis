/* Records crossing between the host and a script, as the canonical ABI lays them out: flattened
 * into the values they hold, in order, or in the script's memory, with each field aligned as its
 * type needs. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_records.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* An instance of the script records.wat, started, with its script and library. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

/* The most hits a host function takes. */
enum { MOST_HITS = 4 };

/* What the host functions that take hits were last given, with a copy of each weapon. */
static struct {
    unsigned calls;
    test_records_host_hit hits[MOST_HITS];
    char *weapons[MOST_HITS];
    size_t count;
} taken;

/* Forgets every call, freeing the copies. */
static void forget_taken(void) {
    for (size_t i = 0; i < MOST_HITS; i++) {
        free(taken.weapons[i]);
    }
    memset(&taken, 0, sizeof taken);
}

/* A copy of `string`'s bytes, with a NUL after them, which the case frees. */
static char *copy_of(stasis_string string) {
    char *copy = malloc(string.size + 1);
    ASSERT_TRUE(copy != NULL);
    if (string.size > 0) {
        memcpy(copy, string.data, string.size);
    }
    copy[string.size] = '\0';
    return copy;
}

/* Records a call that takes the `count` hits at `hits`, forgetting the last. */
static void take_hits(const test_records_host_hit *hits, size_t count) {
    const unsigned calls = taken.calls;
    forget_taken();
    taken.calls = calls + 1;
    taken.count = count;
    for (size_t i = 0; i < count; i++) {
        taken.hits[i] = hits[i];
        taken.weapons[i] = copy_of(hits[i].weapon);
    }
}

void test_records_host_take_hit(stasis_instance *instance, test_records_host_hit blow) {
    (void)instance;
    take_hits(&blow, 1);
}

void test_records_host_take_hits(stasis_instance *instance, test_records_host_hit a,
                                 test_records_host_hit b, test_records_host_hit c,
                                 test_records_host_hit d) {
    (void)instance;
    const test_records_host_hit hits[MOST_HITS] = {a, b, c, d};
    take_hits(hits, MOST_HITS);
}

/* The records `give_hit`, `give_outer` and `give_single` return. */
static test_records_host_hit reply_hit;
static test_records_host_outer reply_outer;
static test_records_host_single reply_single;

test_records_host_hit test_records_host_give_hit(stasis_instance *instance) {
    (void)instance;
    return reply_hit;
}

test_records_host_outer test_records_host_give_outer(stasis_instance *instance) {
    (void)instance;
    return reply_outer;
}

test_records_host_single test_records_host_give_single(stasis_instance *instance) {
    (void)instance;
    return reply_single;
}

static struct running start_records(void) {
    forget_taken();
    struct running running;
    running.library = create_library();
    running.script = load_script(running.library, &test_records_interface, "records");
    running.instance = start_instance(running.script);
    return running;
}

static void stop(struct running running) {
    destroy_all(running.instance, running.script, running.library);
    forget_taken();
}

/* Writes the bytes of `text`, without its NUL, at `address` in the instance's memory. */
static void put(stasis_instance *instance, uint32_t address, const char *text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        ASSERT_INT_EQ(
            test_records_user_store_byte(instance, address + (uint32_t)i, (uint8_t)text[i]),
            STASIS_OK);
    }
}

/* A record argument flattens into the values it holds, in order, the nested `vec2` included, and
 * its string points into the script's memory, as a string argument of its own would. */
TEST(a_host_function_takes_a_record) {
    const struct running running = start_records();
    put(running.instance, 4096, "sword");
    ASSERT_INT_EQ(test_records_user_pass_hit_at(running.instance, 4096, 5, 12, 1.5F, -2.25F),
                  STASIS_OK);
    ASSERT_UINT_EQ(taken.calls, 1);
    ASSERT_STR_EQ(taken.weapons[0], "sword");
    ASSERT_SIZE_EQ(taken.hits[0].weapon.size, 5);
    ASSERT_UINT_EQ(taken.hits[0].damage, 12);
    ASSERT_FLOAT_EQ(taken.hits[0].at.x, 1.5F);
    ASSERT_FLOAT_EQ(taken.hits[0].at.y, -2.25F);
    stop(running);
}

/* `text` as a stasis_string, without its NUL. */
static stasis_string text_of(const char *text) {
    stasis_string string;
    string.data = text;
    string.size = strlen(text);
    return string;
}

/* The 32 bits of a float. */
static uint32_t bits_of(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

/* The 4 bytes at `address` in the instance's memory, little-endian, and the byte. */
static uint32_t load(stasis_instance *instance, uint32_t address) {
    uint32_t value = 0;
    ASSERT_INT_EQ(test_records_user_load(instance, address, &value), STASIS_OK);
    return value;
}

static uint8_t load_byte(stasis_instance *instance, uint32_t address) {
    uint8_t value = 0;
    ASSERT_INT_EQ(test_records_user_load_byte(instance, address, &value), STASIS_OK);
    return value;
}

/* Checks the alignment and size that the script's cabi_realloc was asked for in call `index`. */
static void expect_realloc(stasis_instance *instance, uint32_t index, uint32_t alignment,
                           uint32_t size) {
    uint32_t value = 0;
    ASSERT_INT_EQ(test_records_user_realloc_alignment(instance, index, &value), STASIS_OK);
    ASSERT_UINT_EQ(value, alignment);
    ASSERT_INT_EQ(test_records_user_realloc_size(instance, index, &value), STASIS_OK);
    ASSERT_UINT_EQ(value, size);
}

/* Checks the number of times the script's cabi_realloc has been called. */
static void expect_reallocs(stasis_instance *instance, uint32_t count) {
    uint32_t value = 0;
    ASSERT_INT_EQ(test_records_user_reallocs(instance, &value), STASIS_OK);
    ASSERT_UINT_EQ(value, count);
}

/* An `outer` whose every field differs from the others in each of its bytes. */
static test_records_host_outer sample_outer(void) {
    test_records_host_outer value;
    memset(&value, 0, sizeof value);
    value.tag = 0xAB;
    value.inner.a = 0x11;
    value.inner.b = 0x0102030405060708U;
    value.inner.c = 0x0A0B;
    value.inner.d = true;
    value.inner.e = 1.5F;
    value.inner.f = -3;
    value.last = -2;
    return value;
}

/* Checks that `value` is the sample `outer`. */
static void expect_sample_outer(test_records_host_outer value) {
    ASSERT_UINT_EQ(value.tag, 0xAB);
    ASSERT_UINT_EQ(value.inner.a, 0x11);
    ASSERT_TRUE(value.inner.b == 0x0102030405060708U);
    ASSERT_UINT_EQ(value.inner.c, 0x0A0B);
    ASSERT_TRUE(value.inner.d);
    ASSERT_FLOAT_EQ(value.inner.e, 1.5F);
    ASSERT_INT_EQ(value.inner.f, -3);
    ASSERT_INT_EQ(value.last, -2);
}

/* A record result flattens into more core values than a function can return, so the script passes
 * a return area, which Stasis lays the record out in: the weapon's address and size, the damage,
 * and the `vec2`, 4 bytes each. The weapon goes into memory that the script's cabi_realloc
 * allocates, from 8192. */
TEST(a_host_function_returns_a_record) {
    const struct running running = start_records();
    reply_hit.weapon = text_of("axe");
    reply_hit.damage = 7;
    reply_hit.at.x = 3.5F;
    reply_hit.at.y = -1.0F;
    ASSERT_INT_EQ(test_records_user_fetch_hit_into(running.instance, 4096), STASIS_OK);
    expect_reallocs(running.instance, 1);
    expect_realloc(running.instance, 0, 1, 3);
    ASSERT_UINT_EQ(load(running.instance, 4096), 8192);
    ASSERT_UINT_EQ(load(running.instance, 4100), 3);
    ASSERT_UINT_EQ(load(running.instance, 4104), 7);
    ASSERT_UINT_EQ(load(running.instance, 4108), bits_of(3.5F));
    ASSERT_UINT_EQ(load(running.instance, 4112), bits_of(-1.0F));
    ASSERT_UINT_EQ(load_byte(running.instance, 8192), 'a');
    ASSERT_UINT_EQ(load_byte(running.instance, 8193), 'x');
    ASSERT_UINT_EQ(load_byte(running.instance, 8194), 'e');
    stop(running);
}

/* `outer` lays out as the canonical ABI has it: `tag` at 0, then `inner`, aligned to 8 for its
 * `b`, at 8, with `a` at 8, `b` at 16, `c` at 24, `d` at 26, `e` at 28 and `f` at 32, and `last`
 * after its padding to 40 bytes, at 40. Nothing is written over the padding. */
TEST(lays_out_a_record_result_with_its_padding) {
    const struct running running = start_records();
    for (uint32_t i = 0; i < 48; i++) {
        ASSERT_INT_EQ(test_records_user_store_byte(running.instance, 4096 + i, 0xEE), STASIS_OK);
    }
    reply_outer = sample_outer();
    ASSERT_INT_EQ(test_records_user_fetch_outer_into(running.instance, 4096), STASIS_OK);
    const uint8_t expected[48] = {
        0xAB, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, /* tag */
        0x11, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, /* inner.a */
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, /* inner.b */
        0x0B, 0x0A, 0x01, 0xEE, 0x00, 0x00, 0xC0, 0x3F, /* inner.c, inner.d, inner.e */
        0xFD, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, /* inner.f */
        0xFE, 0xFF, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, /* last */
    };
    for (uint32_t i = 0; i < 48; i++) {
        ASSERT_UINT_EQ(load_byte(running.instance, 4096 + i), expected[i]);
    }
    expect_reallocs(running.instance, 0);
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

/* A hit with the weapon, damage and place given. */
static test_records_host_hit hit_of(const char *weapon, uint32_t damage, float x, float y) {
    test_records_host_hit hit;
    hit.weapon = text_of(weapon);
    hit.damage = damage;
    hit.at.x = x;
    hit.at.y = y;
    return hit;
}

/* Checks that `hit` is the hit with the weapon, damage and place given. */
static void expect_hit(test_records_host_hit hit, const char *weapon, uint32_t damage, float x,
                       float y) {
    ASSERT_TRUE(string_is(hit.weapon, weapon));
    ASSERT_UINT_EQ(hit.damage, damage);
    ASSERT_FLOAT_EQ(hit.at.x, x);
    ASSERT_FLOAT_EQ(hit.at.y, y);
}

/* Checks the number of times the script's cabi_post_* functions have been called. */
static void expect_posts(stasis_instance *instance, uint32_t count) {
    uint32_t value = 0;
    ASSERT_INT_EQ(test_records_user_posts(instance, &value), STASIS_OK);
    ASSERT_UINT_EQ(value, count);
}

/* An entry point's record argument flattens as a host function's does, and its string goes into
 * the script's memory through cabi_realloc, as a string argument of its own would. */
TEST(an_entry_point_takes_a_record) {
    const struct running running = start_records();
    ASSERT_INT_EQ(test_records_user_pass_hit(running.instance, hit_of("club", 3, 0.5F, 0.25F)),
                  STASIS_OK);
    ASSERT_UINT_EQ(taken.calls, 1);
    ASSERT_STR_EQ(taken.weapons[0], "club");
    expect_hit(taken.hits[0], "club", 3, 0.5F, 0.25F);
    expect_reallocs(running.instance, 1);
    expect_realloc(running.instance, 0, 1, 4);
    stop(running);
}

/* The script returns a record as the address of a return area, which Stasis lifts it from. Its
 * string is copied before the entry point's cabi_post_* overwrites it. */
TEST(an_entry_point_returns_a_record) {
    const struct running running = start_records();
    test_records_host_hit result;
    ASSERT_INT_EQ(
        test_records_user_echo_hit(running.instance, hit_of("mace", 9, -4.0F, 8.0F), &result),
        STASIS_OK);
    expect_hit(result, "mace", 9, -4.0F, 8.0F);
    expect_posts(running.instance, 1);
    stop(running);
}

/* The script lays out `outer` in its return area at the offsets the canonical ABI gives, which
 * Stasis lifts each field from. */
TEST(lifts_a_record_result_with_its_padding) {
    const struct running running = start_records();
    test_records_host_outer result;
    ASSERT_INT_EQ(test_records_user_echo_outer(running.instance, sample_outer(), &result),
                  STASIS_OK);
    expect_sample_outer(result);
    expect_posts(running.instance, 1);
    stop(running);
}

/* Every string in a result is copied into the instance's buffer, the one in each nested record
 * included, and each string in an argument goes into memory in order. */
TEST(copies_every_string_in_a_result) {
    const struct running running = start_records();
    test_records_host_pair value;
    value.left = hit_of("bow", 1, 1.0F, 2.0F);
    value.right = hit_of("spear", 2, 3.0F, 4.0F);
    test_records_host_pair result;
    ASSERT_INT_EQ(test_records_user_echo_pair(running.instance, value, &result), STASIS_OK);
    expect_hit(result.left, "bow", 1, 1.0F, 2.0F);
    expect_hit(result.right, "spear", 2, 3.0F, 4.0F);
    expect_reallocs(running.instance, 2);
    expect_realloc(running.instance, 0, 1, 3);
    expect_realloc(running.instance, 1, 1, 5);
    expect_posts(running.instance, 1);
    stop(running);
}

/* A record that flattens into one core value crosses as that value, without a return area, both
 * ways. */
TEST(a_record_of_one_value_crosses_flat) {
    const struct running running = start_records();
    test_records_host_single value;
    value.value = 2.5;
    test_records_host_single result;
    ASSERT_INT_EQ(test_records_user_echo_single(running.instance, value, &result), STASIS_OK);
    ASSERT_DOUBLE_EQ(result.value, 2.5);
    expect_posts(running.instance, 1);
    reply_single.value = -0.125;
    ASSERT_INT_EQ(test_records_user_fetch_single(running.instance, &result), STASIS_OK);
    ASSERT_DOUBLE_EQ(result.value, -0.125);
    expect_reallocs(running.instance, 0);
    stop(running);
}

/* Checks the `count` bytes at `address` in the instance's memory. */
static void expect_bytes(stasis_instance *instance, uint32_t address, const uint8_t *expected,
                         size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_UINT_EQ(load_byte(instance, address + (uint32_t)i), expected[i]);
    }
}

/* Seventeen scalars flatten into more core values than an entry point can take, so Stasis spills
 * them into the script's memory, allocating a tuple of them with cabi_realloc, laid out as the
 * canonical ABI has it, each aligned as its type needs, in 96 bytes aligned to 8. */
TEST(an_entry_points_arguments_spill_past_sixteen_core_values) {
    const struct running running = start_records();
    uint32_t address = 0;
    ASSERT_INT_EQ(test_records_user_seventeen(running.instance, 0x81, -2, 0x01020304,
                                              0x1112131415161718U, 1.5F, -0.5, true, -3, 0xBEEF, -4,
                                              -5, 0x2122232425262728U, -2.0F, 2.0, false, 0x7F,
                                              0x0102030405060708, &address),
                  STASIS_OK);
    expect_reallocs(running.instance, 1);
    expect_realloc(running.instance, 0, 8, 96);
    ASSERT_UINT_EQ(address, 8192);
    const uint8_t expected[96] = {
        0x81, 0x00, 0xFE, 0xFF, 0x04, 0x03, 0x02, 0x01, /* a, b, c */
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, /* d */
        0x00, 0x00, 0xC0, 0x3F, 0x00, 0x00, 0x00, 0x00, /* e */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0xBF, /* f */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* g */
        0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* h */
        0xEF, 0xBE, 0xFC, 0x00, 0xFB, 0xFF, 0xFF, 0xFF, /* i, j, k */
        0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, /* l */
        0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, /* m */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, /* n */
        0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* o, p */
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, /* q */
    };
    expect_bytes(running.instance, address, expected, sizeof expected);
    stop(running);
}

/* Three hits and a byte flatten into sixteen core values, as many as an entry point can take, so
 * they don't spill, and only the hits' weapons go into memory. */
TEST(sixteen_core_values_dont_spill) {
    const struct running running = start_records();
    uint8_t last = 0;
    ASSERT_INT_EQ(test_records_user_sixteen(running.instance, hit_of("a", 1, 0.0F, 0.0F),
                                            hit_of("bb", 2, 0.0F, 0.0F),
                                            hit_of("ccc", 3, 0.0F, 0.0F), 42, &last),
                  STASIS_OK);
    ASSERT_UINT_EQ(last, 42);
    expect_reallocs(running.instance, 3);
    expect_realloc(running.instance, 0, 1, 1);
    expect_realloc(running.instance, 1, 1, 2);
    expect_realloc(running.instance, 2, 1, 3);
    stop(running);
}

/* Four hits flatten into twenty core values, so they spill both ways: Stasis allocates the tuple
 * of them first, and then each weapon in turn, and the script passes the tuple on to the host
 * function as it is, which Stasis reads them back from. Each hit takes 20 bytes, aligned to 4. */
TEST(spilled_records_cross_to_the_script_and_back) {
    const struct running running = start_records();
    ASSERT_INT_EQ(test_records_user_pass_hits(running.instance, hit_of("dagger", 1, 1.0F, -1.0F),
                                              hit_of("", 2, 2.0F, -2.0F),
                                              hit_of("staff", 3, 3.0F, -3.0F),
                                              hit_of("whip", 4, 4.0F, -4.0F)),
                  STASIS_OK);
    ASSERT_UINT_EQ(taken.calls, 1);
    ASSERT_SIZE_EQ(taken.count, 4);
    expect_hit(taken.hits[0], "dagger", 1, 1.0F, -1.0F);
    expect_hit(taken.hits[1], "", 2, 2.0F, -2.0F);
    expect_hit(taken.hits[2], "staff", 3, 3.0F, -3.0F);
    expect_hit(taken.hits[3], "whip", 4, 4.0F, -4.0F);

    expect_reallocs(running.instance, 5);
    expect_realloc(running.instance, 0, 4, 80);
    expect_realloc(running.instance, 1, 1, 6);
    expect_realloc(running.instance, 2, 1, 0);
    expect_realloc(running.instance, 3, 1, 5);
    expect_realloc(running.instance, 4, 1, 4);
    /* The tuple is at 8192, with the hits at 8192, 8212, 8232 and 8252, and the weapons after it,
     * the empty one taking no bytes. */
    ASSERT_UINT_EQ(load(running.instance, 8192), 8272);
    ASSERT_UINT_EQ(load(running.instance, 8196), 6);
    ASSERT_UINT_EQ(load(running.instance, 8200), 1);
    ASSERT_UINT_EQ(load(running.instance, 8204), bits_of(1.0F));
    ASSERT_UINT_EQ(load(running.instance, 8208), bits_of(-1.0F));
    ASSERT_UINT_EQ(load(running.instance, 8212), 8278);
    ASSERT_UINT_EQ(load(running.instance, 8216), 0);
    ASSERT_UINT_EQ(load(running.instance, 8232), 8278);
    ASSERT_UINT_EQ(load(running.instance, 8236), 5);
    ASSERT_UINT_EQ(load(running.instance, 8252), 8283);
    ASSERT_UINT_EQ(load(running.instance, 8256), 4);
    ASSERT_UINT_EQ(load(running.instance, 8260), 4);
    ASSERT_UINT_EQ(load(running.instance, 8268), bits_of(-4.0F));
    stop(running);
}

/* Loads the script <name>.wasm against records.stasis, which must fail with `status` and the
 * message `expected`, leaving no script behind. */
static void expect_refused(const char *name, stasis_status status, const char *expected) {
    stasis_library *library = create_library();
    const struct module_bytes module = read_module(name);
    char message[2048] = "not written";
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, &test_records_interface, module.bytes, module.size,
                                     message, sizeof message, &script),
                  status);
    ASSERT_STR_EQ(message, expected);
    ASSERT_TRUE(script == NULL);
    free_module(module);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* A script needs cabi_realloc when Stasis puts values into its memory: a string in a record that a
 * host function it imports returns, or the arguments of an entry point it provides that spill, even
 * when they hold no string. */
TEST(refuses_a_script_that_lacks_cabi_realloc_where_its_needed) {
    expect_refused("realloc_missing_for_a_record", STASIS_INVALID_SCRIPT,
                   "the script lacks the export `cabi_realloc`, which Stasis needs to put the "
                   "result of the host function `host.give_hit` into its memory");
    expect_refused("realloc_missing_for_spilled_arguments", STASIS_INVALID_SCRIPT,
                   "the script lacks the export `cabi_realloc`, which Stasis needs to put the "
                   "arguments of the entry point `user.seventeen` into its memory");
}

/* Records that the script passes, spilled or not, records without strings that it takes, and its
 * return areas need nothing of the script's. */
TEST(loads_a_script_with_records_that_never_needs_cabi_realloc) {
    forget_taken();
    stasis_library *library = create_library();
    stasis_script *script =
        load_script(library, &test_records_interface, "realloc_not_needed_for_records");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_records_user_pass_hits_at(instance, 1024), STASIS_OK);
    ASSERT_SIZE_EQ(taken.count, 4);
    expect_hit(taken.hits[3], "plain", 7, 0.0F, 0.0F);
    reply_outer.tag = 9;
    ASSERT_INT_EQ(test_records_user_fetch_outer_into(instance, 4096), STASIS_OK);
    ASSERT_UINT_EQ(load_byte(instance, 4096), 9);
    test_records_host_outer value;
    memset(&value, 0, sizeof value);
    value.tag = 5;
    test_records_host_outer result;
    ASSERT_INT_EQ(test_records_user_echo_outer(instance, value, &result), STASIS_OK);
    ASSERT_UINT_EQ(result.tag, 5);
    destroy_all(instance, script, library);
    forget_taken();
}

/* Checks that the last call into `instance` trapped as `kind` with `message`. */
static void expect_trap(stasis_instance *instance, stasis_trap_kind kind, const char *message) {
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, kind);
    ASSERT_STR_EQ(trap->message, message);
}

/* Stores `value` at `address` in the instance's memory, as 4 bytes, little-endian. */
static void store(stasis_instance *instance, uint32_t address, uint32_t value) {
    ASSERT_INT_EQ(test_records_user_store(instance, address, value), STASIS_OK);
}

/* Lays out a hit at `at` in the instance's memory, with the weapon of `size` bytes at `weapon`, a
 * damage of `damage`, and a place of 0, 0. */
static void lay_out_hit(stasis_instance *instance, uint32_t at, uint32_t weapon, uint32_t size,
                        uint32_t damage) {
    store(instance, at, weapon);
    store(instance, at + 4, size);
    store(instance, at + 8, damage);
    store(instance, at + 12, 0);
    store(instance, at + 16, 0);
}

/* Lays out four hits at 4096, as `host.take_hits` takes them, each with the weapon "sword" at 2048
 * but the second, whose weapon is the `size` bytes at `weapon`. */
static void lay_out_hits(stasis_instance *instance, uint32_t weapon, uint32_t size) {
    put(instance, 2048, "sword");
    lay_out_hit(instance, 4096, 2048, 5, 1);
    lay_out_hit(instance, 4116, weapon, size, 2);
    lay_out_hit(instance, 4136, 2048, 5, 3);
    lay_out_hit(instance, 4156, 2048, 5, 4);
}

/* Calls `pass_hits_at` with `address` in a new instance of records.wasm, once `weapon` and `size`
 * have laid out its hits at 4096, which must trap as a boundary violation with `message`, before
 * the host function runs. */
static void expect_hits_refused(uint32_t weapon, uint32_t size, uint32_t address,
                                const char *message) {
    const struct running running = start_records();
    lay_out_hits(running.instance, weapon, size);
    ASSERT_INT_EQ(test_records_user_pass_hits_at(running.instance, address), STASIS_TRAP);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION, message);
    stop(running);
}

/* Spilled arguments that hold records are as aligned as the strictest value they hold, 4 for a
 * hit, and the four hits take 80 bytes. */
TEST(spilled_records_outside_memory_or_misaligned_trap) {
    expect_hits_refused(2048, 5, 4098,
                        "the script passed the arguments of `host.take_hits` at 4098 in its "
                        "memory, which isn't a multiple of their alignment, 4");
    expect_hits_refused(2048, 5, 65520,
                        "the script passed the arguments of `host.take_hits` at 65520 in its "
                        "memory, where their 80 bytes run past its end at 65536");
}

/* A string in a record is checked as a string argument is, and a message names it by its field and
 * the argument that holds the record. */
TEST(a_string_in_a_record_argument_that_cant_cross_traps) {
    expect_hits_refused(65534, 5, 4096,
                        "the script passed the field `weapon` of argument 2 of `host.take_hits` at "
                        "65534 in its memory, where its 5 bytes run past its end at 65536");
    const struct running running = start_records();
    put(running.instance, 4000, "\xff");
    ASSERT_INT_EQ(test_records_user_pass_hit_at(running.instance, 4000, 1, 0, 0.0F, 0.0F),
                  STASIS_TRAP);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed the field `weapon` of argument 1 of `host.take_hit` as a "
                "string of 1 bytes that isn't UTF-8, from offset 0");
    stop(running);
}

/* A return area for a record is as aligned as the record, 8 for an `outer`, and as large: 20 bytes
 * for a hit. */
TEST(a_return_area_passed_for_a_record_outside_memory_or_misaligned_traps) {
    struct running running = start_records();
    ASSERT_INT_EQ(test_records_user_fetch_outer_into(running.instance, 4100), STASIS_TRAP);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed the return area of `host.give_outer` at 4100 in its memory, "
                "which isn't a multiple of its alignment, 8");
    stop(running);
    running = start_records();
    ASSERT_INT_EQ(test_records_user_fetch_hit_into(running.instance, 65520), STASIS_TRAP);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                "the script passed the return area of `host.give_hit` at 65520 in its memory, "
                "where its 20 bytes run past its end at 65536");
    stop(running);
}

/* Calls `fetch_hit_into` with `give_hit` replying a hit whose weapon is `weapon`, which must trap
 * as a host abort with `message`, before anything goes into the script's memory. */
static void expect_reply_refused(stasis_string weapon, const char *message) {
    const struct running running = start_records();
    reply_hit = hit_of("", 1, 0.0F, 0.0F);
    reply_hit.weapon = weapon;
    ASSERT_INT_EQ(test_records_user_fetch_hit_into(running.instance, 4096), STASIS_TRAP);
    expect_trap(running.instance, STASIS_TRAP_KIND_HOST_ABORT, message);
    stop(running);
}

/* A string in a record that a host function returns is checked as a string result is. */
TEST(a_host_function_returning_a_record_whose_string_cant_cross_traps) {
    const stasis_string null_data = {NULL, 3};
    expect_reply_refused(null_data, "the host function `host.give_hit` returned the field `weapon` "
                                    "of its result as a string of 3 bytes whose data is null");
    expect_reply_refused(text_of("ok\xc3"),
                         "the host function `host.give_hit` returned the field `weapon` of its "
                         "result as a string of 3 bytes that isn't UTF-8, from offset 2");
}

/* The modes of records.wat's cabi_realloc. */
enum {
    REALLOC_NULL = 1,
    REALLOC_MISALIGNED = 2,
    REALLOC_PAST_THE_END = 3,
    REALLOC_GROWING = 4,
};

/* Starts a new instance of records.wasm whose cabi_realloc does as `mode` says once it has been
 * called `after` times. */
static struct running start_reallocating(uint32_t mode, uint32_t after) {
    const struct running running = start_records();
    ASSERT_INT_EQ(test_records_user_set_realloc(running.instance, mode, after), STASIS_OK);
    return running;
}

/* Four hits, which spill. */
static stasis_status pass_four_hits(stasis_instance *instance) {
    return test_records_user_pass_hits(instance, hit_of("dagger", 1, 1.0F, -1.0F),
                                       hit_of("", 2, 2.0F, -2.0F), hit_of("staff", 3, 3.0F, -3.0F),
                                       hit_of("whip", 4, 4.0F, -4.0F));
}

/* Calls `pass_hits` in a new instance whose cabi_realloc does as `mode` says once it has been
 * called `after` times, which must trap as `kind` with `message`. */
static void expect_pass_hits_traps(uint32_t mode, uint32_t after, stasis_trap_kind kind,
                                   const char *message) {
    const struct running running = start_reallocating(mode, after);
    ASSERT_INT_EQ(pass_four_hits(running.instance), STASIS_TRAP);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_trap(running.instance, kind, message);
    stop(running);
}

/* A message names what cabi_realloc failed to allocate: spilled arguments as a whole, or a string
 * in a record by its field and the argument or result that holds the record. */
TEST(a_null_from_cabi_realloc_for_a_record_traps) {
    expect_pass_hits_traps(REALLOC_NULL, 0, STASIS_TRAP_KIND_ALLOCATION_FAILED,
                           "the script's `cabi_realloc` returned null for the 80 bytes of the "
                           "arguments of `user.pass_hits`");
    expect_pass_hits_traps(REALLOC_NULL, 1, STASIS_TRAP_KIND_ALLOCATION_FAILED,
                           "the script's `cabi_realloc` returned null for the 6 bytes of the field "
                           "`weapon` of argument 1 of `user.pass_hits`");
    expect_pass_hits_traps(REALLOC_NULL, 3, STASIS_TRAP_KIND_ALLOCATION_FAILED,
                           "the script's `cabi_realloc` returned null for the 5 bytes of the field "
                           "`weapon` of argument 3 of `user.pass_hits`");

    struct running running = start_reallocating(REALLOC_NULL, 0);
    ASSERT_INT_EQ(test_records_user_pass_hit(running.instance, hit_of("club", 3, 0.0F, 0.0F)),
                  STASIS_TRAP);
    expect_trap(running.instance, STASIS_TRAP_KIND_ALLOCATION_FAILED,
                "the script's `cabi_realloc` returned null for the 4 bytes of the field `weapon` "
                "of argument 1 of `user.pass_hit`");
    stop(running);
    running = start_reallocating(REALLOC_NULL, 0);
    reply_hit = hit_of("axe", 7, 0.0F, 0.0F);
    ASSERT_INT_EQ(test_records_user_fetch_hit_into(running.instance, 4096), STASIS_TRAP);
    expect_trap(running.instance, STASIS_TRAP_KIND_ALLOCATION_FAILED,
                "the script's `cabi_realloc` returned null for the 3 bytes of the field `weapon` "
                "of the result of `host.give_hit`");
    stop(running);
}

/* The tuple of spilled arguments must be as aligned as they need, and all in memory. */
TEST(a_tuple_allocated_outside_memory_or_misaligned_traps) {
    expect_pass_hits_traps(REALLOC_MISALIGNED, 0, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                           "the script's `cabi_realloc` returned 8193 for the 80 bytes of the "
                           "arguments of `user.pass_hits`, which isn't a multiple of their "
                           "alignment, 4");
    expect_pass_hits_traps(REALLOC_PAST_THE_END, 0, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                           "the script's `cabi_realloc` returned 65460 for the 80 bytes of the "
                           "arguments of `user.pass_hits`, where they run past the end of its "
                           "memory at 65536");
}

/* Calls `return_outer_at` or `return_pair_at` with `area`, in an instance that `lay_out` has laid
 * out, which must trap as a boundary violation with `message`. */
static void expect_return_refused(void (*lay_out)(stasis_instance *instance), bool pair,
                                  uint32_t area, const char *message) {
    const struct running running = start_records();
    lay_out(running.instance);
    if (pair) {
        test_records_host_pair result;
        ASSERT_INT_EQ(test_records_user_return_pair_at(running.instance, area, &result),
                      STASIS_TRAP);
    } else {
        test_records_host_outer result;
        ASSERT_INT_EQ(test_records_user_return_outer_at(running.instance, area, &result),
                      STASIS_TRAP);
    }
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION, message);
    stop(running);
}

static void lay_out_nothing(stasis_instance *instance) { (void)instance; }

/* Lays out a pair at 4096 whose right weapon runs past the end of memory. */
static void lay_out_pair_past_the_end(stasis_instance *instance) {
    put(instance, 2048, "sword");
    lay_out_hit(instance, 4096, 2048, 5, 1);
    lay_out_hit(instance, 4116, 65534, 5, 2);
}

/* Lays out a pair at 4096 whose left weapon isn't UTF-8. */
static void lay_out_pair_not_utf8(stasis_instance *instance) {
    put(instance, 2048, "\xff");
    lay_out_hit(instance, 4096, 2048, 1, 1);
    lay_out_hit(instance, 4116, 2048, 0, 2);
}

/* An entry point's return area for a record is checked as a host function's is, and each string in
 * the record as a string result is, named by its fields, outermost first. */
TEST(a_record_result_that_cant_cross_traps) {
    expect_return_refused(lay_out_nothing, false, 4100,
                          "the entry point `user.return_outer_at` returned its result at 4100 in "
                          "the script's memory, which isn't a multiple of its alignment, 8");
    expect_return_refused(lay_out_nothing, true, 65500,
                          "the entry point `user.return_pair_at` returned its result at 65500 in "
                          "the script's memory, where its 40 bytes run past its end at 65536");
    expect_return_refused(lay_out_pair_past_the_end, true, 4096,
                          "the entry point `user.return_pair_at` returned the field `right.weapon` "
                          "of its result as a string at 65534 in the script's memory, where its 5 "
                          "bytes run past its end at 65536");
    expect_return_refused(lay_out_pair_not_utf8, true, 4096,
                          "the entry point `user.return_pair_at` returned the field `left.weapon` "
                          "of its result as a string of 1 bytes that isn't UTF-8, from offset 0");
}

/* A string in a record that the host passes must cross as a string argument must, or the call is
 * refused before anything goes into the script's memory. */
TEST(refuses_a_record_argument_whose_string_cant_cross) {
    const struct running running = start_records();
    test_records_host_hit blow = hit_of("", 3, 0.0F, 0.0F);
    blow.weapon.data = NULL;
    blow.weapon.size = 3;
    ASSERT_INT_EQ(test_records_user_pass_hit(running.instance, blow), STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(test_records_user_pass_hits(
                      running.instance, hit_of("a", 1, 0.0F, 0.0F), hit_of("b", 2, 0.0F, 0.0F),
                      hit_of("\xc0", 3, 0.0F, 0.0F), hit_of("d", 4, 0.0F, 0.0F)),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_UINT_EQ(taken.calls, 0);
    expect_reallocs(running.instance, 0);
    stop(running);
}

/* cabi_realloc grows memory each time Stasis calls it, which can move it, so Stasis takes where the
 * memory is afresh as it lays out spilled arguments and a host function's record result. */
TEST(records_cross_when_cabi_realloc_grows_memory) {
    const struct running running = start_reallocating(REALLOC_GROWING, 0);
    ASSERT_INT_EQ(pass_four_hits(running.instance), STASIS_OK);
    ASSERT_SIZE_EQ(taken.count, 4);
    expect_hit(taken.hits[0], "dagger", 1, 1.0F, -1.0F);
    expect_hit(taken.hits[1], "", 2, 2.0F, -2.0F);
    expect_hit(taken.hits[2], "staff", 3, 3.0F, -3.0F);
    expect_hit(taken.hits[3], "whip", 4, 4.0F, -4.0F);
    /* The weapon is where the sixth allocation grew memory to: the first page, and five allocations
     * of 16 pages each, on. */
    reply_hit = hit_of("axe", 7, 3.5F, -1.0F);
    ASSERT_INT_EQ(test_records_user_fetch_hit_into(running.instance, 4096), STASIS_OK);
    const uint32_t weapon = 81U * 65536U;
    ASSERT_UINT_EQ(load(running.instance, 4096), weapon);
    ASSERT_UINT_EQ(load(running.instance, 4100), 3);
    ASSERT_UINT_EQ(load(running.instance, 4104), 7);
    ASSERT_UINT_EQ(load_byte(running.instance, weapon), 'a');
    stop(running);
}

/* A C script built as a script author builds one holds records in C structs, which the compiler
 * lays out as the canonical ABI does for these records. Stasis lifts an `outer` that the script
 * filled in, and the script reads each field of one that Stasis laid out, and of the hits that
 * Stasis spilled, which it frees with the C library's free. */
TEST(a_c_script_lays_out_records_as_c_does) {
    forget_taken();
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_records_interface, "c_records");
    stasis_instance *instance = start_instance(script);
    test_records_host_outer result;
    ASSERT_INT_EQ(test_records_user_echo_outer(instance, sample_outer(), &result), STASIS_OK);
    expect_sample_outer(result);

    reply_outer = sample_outer();
    ASSERT_INT_EQ(test_records_user_fetch_outer(instance, &result), STASIS_OK);
    ASSERT_UINT_EQ(result.tag, 0xAC);
    ASSERT_UINT_EQ(result.inner.a, 0x12);
    ASSERT_TRUE(result.inner.b == 0x0102030405060709U);
    ASSERT_UINT_EQ(result.inner.c, 0x0A0C);
    ASSERT_TRUE(!result.inner.d);
    ASSERT_FLOAT_EQ(result.inner.e, 2.5F);
    ASSERT_INT_EQ(result.inner.f, -2);
    ASSERT_INT_EQ(result.last, -1);

    /* The script passes each hit on in turn, so the host sees the last. */
    ASSERT_INT_EQ(pass_four_hits(instance), STASIS_OK);
    ASSERT_UINT_EQ(taken.calls, 4);
    expect_hit(taken.hits[0], "whip", 4, 4.0F, -4.0F);
    destroy_all(instance, script, library);
    forget_taken();
}

TEST_LIST(TEST_CASE(a_host_function_takes_a_record), TEST_CASE(a_host_function_returns_a_record),
          TEST_CASE(lays_out_a_record_result_with_its_padding),
          TEST_CASE(an_entry_point_takes_a_record), TEST_CASE(an_entry_point_returns_a_record),
          TEST_CASE(lifts_a_record_result_with_its_padding),
          TEST_CASE(copies_every_string_in_a_result), TEST_CASE(a_record_of_one_value_crosses_flat),
          TEST_CASE(an_entry_points_arguments_spill_past_sixteen_core_values),
          TEST_CASE(sixteen_core_values_dont_spill),
          TEST_CASE(spilled_records_cross_to_the_script_and_back),
          TEST_CASE(refuses_a_script_that_lacks_cabi_realloc_where_its_needed),
          TEST_CASE(loads_a_script_with_records_that_never_needs_cabi_realloc),
          TEST_CASE(spilled_records_outside_memory_or_misaligned_trap),
          TEST_CASE(a_string_in_a_record_argument_that_cant_cross_traps),
          TEST_CASE(a_return_area_passed_for_a_record_outside_memory_or_misaligned_traps),
          TEST_CASE(a_host_function_returning_a_record_whose_string_cant_cross_traps),
          TEST_CASE(a_null_from_cabi_realloc_for_a_record_traps),
          TEST_CASE(a_tuple_allocated_outside_memory_or_misaligned_traps),
          TEST_CASE(a_record_result_that_cant_cross_traps),
          TEST_CASE(refuses_a_record_argument_whose_string_cant_cross),
          TEST_CASE(records_cross_when_cabi_realloc_grows_memory),
          TEST_CASE(a_c_script_lays_out_records_as_c_does));
