/* Host functions: scripts calling the functions their interface declares, with every scalar type
 * crossing both ways and arguments spilling into the script's memory, the load check of a script's
 * host functions, host functions that abort the call, calls that never nest, and the stack a host
 * function is sure of. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_calls.h"
#include "test_hosts.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The host functions of hosts.stasis, which record what they're given and return what the case has
 * set. */

/* A value of each scalar type: what a host function was last given, or what it replies. */
struct scalars {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    int8_t s8;
    int16_t s16;
    int32_t s32;
    int64_t s64;
    float f32;
    double f64;
    bool boolean;
};

static struct scalars passed;
static struct scalars replies;

uint8_t test_hosts_host_pass_u8(stasis_instance *instance, uint8_t value) {
    (void)instance;
    passed.u8 = value;
    return replies.u8;
}

uint16_t test_hosts_host_pass_u16(stasis_instance *instance, uint16_t value) {
    (void)instance;
    passed.u16 = value;
    return replies.u16;
}

uint32_t test_hosts_host_pass_u32(stasis_instance *instance, uint32_t value) {
    (void)instance;
    passed.u32 = value;
    return replies.u32;
}

uint64_t test_hosts_host_pass_u64(stasis_instance *instance, uint64_t value) {
    (void)instance;
    passed.u64 = value;
    return replies.u64;
}

int8_t test_hosts_host_pass_s8(stasis_instance *instance, int8_t value) {
    (void)instance;
    passed.s8 = value;
    return replies.s8;
}

int16_t test_hosts_host_pass_s16(stasis_instance *instance, int16_t value) {
    (void)instance;
    passed.s16 = value;
    return replies.s16;
}

int32_t test_hosts_host_pass_s32(stasis_instance *instance, int32_t value) {
    (void)instance;
    passed.s32 = value;
    return replies.s32;
}

int64_t test_hosts_host_pass_s64(stasis_instance *instance, int64_t value) {
    (void)instance;
    passed.s64 = value;
    return replies.s64;
}

float test_hosts_host_pass_f32(stasis_instance *instance, float value) {
    (void)instance;
    passed.f32 = value;
    return replies.f32;
}

double test_hosts_host_pass_f64(stasis_instance *instance, double value) {
    (void)instance;
    passed.f64 = value;
    return replies.f64;
}

bool test_hosts_host_pass_bool(stasis_instance *instance, bool value) {
    (void)instance;
    passed.boolean = value;
    return replies.boolean;
}

/* What `host.ping` does, if the case has set it, and how many times it has been called. */
static void (*on_ping)(stasis_instance *instance);
static unsigned pings;

void test_hosts_host_ping(stasis_instance *instance) {
    pings++;
    if (on_ping != NULL) {
        on_ping(instance);
    }
}

static uint32_t bits_of_f32(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint64_t bits_of_f64(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

/* The arguments `sixteen` or `seventeen` was last given, in order, each as 64 bits: an unsigned
 * integer as it is, a signed one extended by its sign, a float as its bits, and a bool as 0 or 1.
 */
static uint64_t spread[17];

/* What `sixteen` and `seventeen` return. */
static uint64_t spread_reply;

/* Records the sixteen arguments the two functions share. */
static void record_sixteen(uint8_t a, int16_t b, uint32_t c, uint64_t d, float e, double f, bool g,
                           int64_t h, uint16_t i, int8_t j, int32_t k, uint64_t l, float m,
                           double n, bool o, uint8_t p) {
    const uint64_t arguments[16] = {
        a,
        (uint64_t)(int64_t)b,
        c,
        d,
        bits_of_f32(e),
        bits_of_f64(f),
        g ? 1 : 0,
        (uint64_t)h,
        i,
        (uint64_t)(int64_t)j,
        (uint64_t)(int64_t)k,
        l,
        bits_of_f32(m),
        bits_of_f64(n),
        o ? 1 : 0,
        p,
    };
    memcpy(spread, arguments, sizeof arguments);
}

uint64_t test_hosts_host_sixteen(stasis_instance *instance, uint8_t a, int16_t b, uint32_t c,
                                 uint64_t d, float e, double f, bool g, int64_t h, uint16_t i,
                                 int8_t j, int32_t k, uint64_t l, float m, double n, bool o,
                                 uint8_t p) {
    (void)instance;
    record_sixteen(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p);
    spread[16] = 0;
    return spread_reply;
}

uint64_t test_hosts_host_seventeen(stasis_instance *instance, uint8_t a, int16_t b, uint32_t c,
                                   uint64_t d, float e, double f, bool g, int64_t h, uint16_t i,
                                   int8_t j, int32_t k, uint64_t l, float m, double n, bool o,
                                   uint8_t p, int64_t q) {
    (void)instance;
    record_sixteen(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p);
    spread[16] = (uint64_t)q;
    return spread_reply;
}

void test_hosts_host_thing_poke(stasis_instance *instance, test_hosts_host_thing *self) {
    (void)instance;
    (void)self;
}

/* How many times `other.ping` has been called, which it returns. */
static uint32_t other_pings;

uint32_t test_hosts_other_ping(stasis_instance *instance) {
    (void)instance;
    return ++other_pings;
}

/* An instance of the script hosts.wat, started, with its script and library. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

static struct running start_hosts(void) {
    struct running running;
    running.library = create_library();
    running.script = load_script(running.library, &test_hosts_interface, "hosts");
    running.instance = start_instance(running.script);
    return running;
}

static void stop(struct running running) {
    destroy_all(running.instance, running.script, running.library);
}

static float f32_of(uint32_t bits) {
    float value = 0.0F;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static double f64_of(uint64_t bits) {
    double value = 0.0;
    memcpy(&value, &bits, sizeof value);
    return value;
}

TEST(a_script_calls_a_host_function) {
    const struct running running = start_hosts();
    replies.u32 = 0xDEADBEEFU;
    uint32_t result = 0;
    ASSERT_INT_EQ(test_hosts_caller_relay_u32(running.instance, 0x12345678U, &result), STASIS_OK);
    ASSERT_UINT_EQ(passed.u32, 0x12345678U);
    ASSERT_UINT_EQ(result, 0xDEADBEEFU);
    stop(running);
}

/* Each group's host functions are imported from a module of their own, so `other.ping` and
 * `host.ping` are apart. */
TEST(calls_a_host_function_of_each_group) {
    const struct running running = start_hosts();
    pings = 0;
    other_pings = 0;
    uint32_t result = 0;
    ASSERT_INT_EQ(test_hosts_caller_ping_other(running.instance, &result), STASIS_OK);
    ASSERT_UINT_EQ(result, 1);
    ASSERT_INT_EQ(test_hosts_caller_ping_other(running.instance, &result), STASIS_OK);
    ASSERT_UINT_EQ(result, 2);
    ASSERT_UINT_EQ(pings, 0);
    ASSERT_INT_EQ(test_hosts_caller_ping(running.instance), STASIS_OK);
    ASSERT_UINT_EQ(pings, 2);
    ASSERT_UINT_EQ(other_pings, 2);
    stop(running);
}

/* A method takes the handle of the host object it's called on, and 0 is never a handle, so a script
 * that calls one on 0 traps as a boundary violation before the method runs. */
TEST(a_method_called_on_handle_0_traps) {
    const struct running running = start_hosts();
    ASSERT_INT_EQ(test_hosts_caller_poke(running.instance), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(running.instance);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_BOUNDARY_VIOLATION);
    stop(running);
}

/* Each argument reaches the host function as the script passed it, and each result reaches the
 * script as the host function returned it. */
TEST(passes_unsigned_integers_both_ways) {
    const struct running running = start_hosts();
    stasis_instance *instance = running.instance;
    uint8_t u8 = 0;
    replies.u8 = 254;
    ASSERT_INT_EQ(test_hosts_caller_relay_u8(instance, 255, &u8), STASIS_OK);
    ASSERT_UINT_EQ(passed.u8, 255);
    ASSERT_UINT_EQ(u8, 254);
    uint16_t u16 = 0;
    replies.u16 = 65534;
    ASSERT_INT_EQ(test_hosts_caller_relay_u16(instance, 65535, &u16), STASIS_OK);
    ASSERT_UINT_EQ(passed.u16, 65535);
    ASSERT_UINT_EQ(u16, 65534);
    uint32_t u32 = 0;
    replies.u32 = 0x80000000U;
    ASSERT_INT_EQ(test_hosts_caller_relay_u32(instance, 0xFFFFFFFFU, &u32), STASIS_OK);
    ASSERT_UINT_EQ(passed.u32, 0xFFFFFFFFU);
    ASSERT_UINT_EQ(u32, 0x80000000U);
    uint64_t u64 = 0;
    replies.u64 = 0x8000000000000001U;
    ASSERT_INT_EQ(test_hosts_caller_relay_u64(instance, 0xFFFFFFFFFFFFFFFFU, &u64), STASIS_OK);
    ASSERT_UINT_EQ(passed.u64, 0xFFFFFFFFFFFFFFFFU);
    ASSERT_UINT_EQ(u64, 0x8000000000000001U);
    stop(running);
}

TEST(passes_signed_integers_both_ways) {
    const struct running running = start_hosts();
    stasis_instance *instance = running.instance;
    int8_t s8 = 0;
    replies.s8 = 127;
    ASSERT_INT_EQ(test_hosts_caller_relay_s8(instance, -128, &s8), STASIS_OK);
    ASSERT_INT_EQ(passed.s8, -128);
    ASSERT_INT_EQ(s8, 127);
    int16_t s16 = 0;
    replies.s16 = -1;
    ASSERT_INT_EQ(test_hosts_caller_relay_s16(instance, -32768, &s16), STASIS_OK);
    ASSERT_INT_EQ(passed.s16, -32768);
    ASSERT_INT_EQ(s16, -1);
    int32_t s32 = 0;
    replies.s32 = INT32_MAX;
    ASSERT_INT_EQ(test_hosts_caller_relay_s32(instance, INT32_MIN, &s32), STASIS_OK);
    ASSERT_INT_EQ(passed.s32, INT32_MIN);
    ASSERT_INT_EQ(s32, INT32_MAX);
    int64_t s64 = 0;
    replies.s64 = -2;
    ASSERT_INT_EQ(test_hosts_caller_relay_s64(instance, INT64_MIN, &s64), STASIS_OK);
    ASSERT_INT_EQ(passed.s64, INT64_MIN);
    ASSERT_INT_EQ(s64, -2);
    stop(running);
}

/* Negative zero, infinity, a subnormal and NaNs with payloads cross as they are, in both
 * directions. */
TEST(passes_floats_bit_for_bit_both_ways) {
    const struct running running = start_hosts();
    stasis_instance *instance = running.instance;
    const uint32_t f32_bits[] = {0x80000000U, 0x7F800000U, 0x00000001U, 0x7FC12345U, 0xFFA00001U};
    for (size_t i = 0; i < sizeof f32_bits / sizeof f32_bits[0]; i++) {
        const uint32_t reply = f32_bits[(i + 1) % (sizeof f32_bits / sizeof f32_bits[0])];
        replies.f32 = f32_of(reply);
        float f32 = 0.0F;
        ASSERT_INT_EQ(test_hosts_caller_relay_f32(instance, f32_of(f32_bits[i]), &f32), STASIS_OK);
        ASSERT_UINT_EQ(bits_of_f32(passed.f32), f32_bits[i]);
        ASSERT_UINT_EQ(bits_of_f32(f32), reply);
    }
    const uint64_t f64_bits[] = {0x8000000000000000U, 0xFFF0000000000000U, 0x0000000000000001U,
                                 0x7FF8000000012345U, 0xFFF4000000000001U};
    for (size_t i = 0; i < sizeof f64_bits / sizeof f64_bits[0]; i++) {
        const uint64_t reply = f64_bits[(i + 1) % (sizeof f64_bits / sizeof f64_bits[0])];
        replies.f64 = f64_of(reply);
        double f64 = 0.0;
        ASSERT_INT_EQ(test_hosts_caller_relay_f64(instance, f64_of(f64_bits[i]), &f64), STASIS_OK);
        ASSERT_UINT_EQ(bits_of_f64(passed.f64), f64_bits[i]);
        ASSERT_UINT_EQ(bits_of_f64(f64), reply);
    }
    stop(running);
}

TEST(passes_bools_both_ways) {
    const struct running running = start_hosts();
    bool result = true;
    replies.boolean = false;
    ASSERT_INT_EQ(test_hosts_caller_relay_bool(running.instance, true, &result), STASIS_OK);
    ASSERT_TRUE(passed.boolean);
    ASSERT_TRUE(!result);
    replies.boolean = true;
    ASSERT_INT_EQ(test_hosts_caller_relay_bool(running.instance, false, &result), STASIS_OK);
    ASSERT_TRUE(!passed.boolean);
    ASSERT_TRUE(result);
    stop(running);
}

/* An argument narrower than 32 bits keeps the low bits of what the script passes, and a bool is
 * true unless the script passes 0. */
TEST(narrows_arguments_to_their_type) {
    const struct running running = start_hosts();
    stasis_instance *instance = running.instance;
    uint32_t ignored = 0;
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_u8(instance, 0x1FF, &ignored), STASIS_OK);
    ASSERT_UINT_EQ(passed.u8, 0xFF);
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_u8(instance, 0xFFFFFF00U, &ignored), STASIS_OK);
    ASSERT_UINT_EQ(passed.u8, 0);
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_u16(instance, 0x12345678, &ignored), STASIS_OK);
    ASSERT_UINT_EQ(passed.u16, 0x5678);
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_s8(instance, 0x80, &ignored), STASIS_OK);
    ASSERT_INT_EQ(passed.s8, -128);
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_s8(instance, 0x17F, &ignored), STASIS_OK);
    ASSERT_INT_EQ(passed.s8, 127);
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_s16(instance, 0x18000, &ignored), STASIS_OK);
    ASSERT_INT_EQ(passed.s16, -32768);
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_bool(instance, 2, &ignored), STASIS_OK);
    ASSERT_TRUE(passed.boolean);
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_bool(instance, 0x100, &ignored), STASIS_OK);
    ASSERT_TRUE(passed.boolean);
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_bool(instance, 0, &ignored), STASIS_OK);
    ASSERT_TRUE(!passed.boolean);
    stop(running);
}

/* The script sees an unsigned result extended with zeros, a signed one with its sign, and a bool
 * as 0 or 1. */
TEST(widens_narrow_results) {
    const struct running running = start_hosts();
    stasis_instance *instance = running.instance;
    uint32_t bits = 0;
    replies.u8 = 255;
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_u8(instance, 0, &bits), STASIS_OK);
    ASSERT_UINT_EQ(bits, 255);
    replies.u16 = 65535;
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_u16(instance, 0, &bits), STASIS_OK);
    ASSERT_UINT_EQ(bits, 65535);
    replies.s8 = -1;
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_s8(instance, 0, &bits), STASIS_OK);
    ASSERT_UINT_EQ(bits, 0xFFFFFFFFU);
    replies.s16 = -32768;
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_s16(instance, 0, &bits), STASIS_OK);
    ASSERT_UINT_EQ(bits, 0xFFFF8000U);
    replies.boolean = true;
    ASSERT_INT_EQ(test_hosts_caller_relay_bits_bool(instance, 0, &bits), STASIS_OK);
    ASSERT_UINT_EQ(bits, 1);
    stop(running);
}

/* The arguments call_sixteen and call_seventeen pass, as `spread` records them. */
static const uint64_t expected_spread[17] = {
    200,
    0xFFFFFFFFFFFFFFFEU,
    4000000000U,
    0x123456789ABCDEF0U,
    0x3FC00000U,
    0xC002000000000000U,
    1,
    0xFFFFFFFFFFFFFFFBU,
    65535,
    0xFFFFFFFFFFFFFF80U,
    0xFFFFFFFF80000000U,
    0xFFFFFFFFFFFFFFFFU,
    0x7FC12345U,
    0x8000000000000000U,
    0,
    7,
    0x8000000000000000U,
};

/* Checks that `spread` holds the first `count` arguments of expected_spread. */
static void expect_spread(size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_UINT_EQ(spread[i], expected_spread[i]);
    }
}

/* Sixteen arguments flatten into as many core parameters, the most there can be. */
TEST(passes_sixteen_arguments_as_core_parameters) {
    const struct running running = start_hosts();
    spread_reply = 0xFEDCBA9876543210U;
    uint64_t result = 0;
    ASSERT_INT_EQ(test_hosts_caller_call_sixteen(running.instance, &result), STASIS_OK);
    expect_spread(16);
    ASSERT_UINT_EQ(result, 0xFEDCBA9876543210U);
    stop(running);
}

/* Seventeen arguments are one too many, so the script passes the address of a tuple of them in its
 * memory, from which Stasis reads each. */
TEST(reads_spilled_arguments_from_the_scripts_memory) {
    const struct running running = start_hosts();
    spread_reply = 42;
    uint64_t result = 0;
    ASSERT_INT_EQ(test_hosts_caller_call_seventeen(running.instance, &result), STASIS_OK);
    expect_spread(17);
    ASSERT_UINT_EQ(result, 42);
    stop(running);
}

/* The spilled arguments take 96 bytes, so the last address they fit at in the script's one page is
 * 65440, where it reads zeros. */
TEST(reads_spilled_arguments_that_end_where_memory_does) {
    const struct running running = start_hosts();
    memset(spread, 0xFF, sizeof spread);
    uint64_t result = 0;
    ASSERT_INT_EQ(test_hosts_caller_call_seventeen_at(running.instance, 65440, &result), STASIS_OK);
    for (size_t i = 0; i < 17; i++) {
        ASSERT_UINT_EQ(spread[i], 0);
    }
    stop(running);
}

/* Calls `call_seventeen_at` with `address` in a new instance of hosts.wasm, which must trap as a
 * boundary violation with `message`, before the host function runs. */
static void expect_spill_refused(uint32_t address, const char *message) {
    const struct running running = start_hosts();
    memset(spread, 0xFF, sizeof spread);
    uint64_t result = 1;
    ASSERT_INT_EQ(test_hosts_caller_call_seventeen_at(running.instance, address, &result),
                  STASIS_TRAP);
    ASSERT_UINT_EQ(result, 0);
    ASSERT_UINT_EQ(spread[0], 0xFFFFFFFFFFFFFFFFU);
    const stasis_trap *trap = stasis_instance_trap(running.instance);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_BOUNDARY_VIOLATION);
    ASSERT_STR_EQ(trap->message, message);
    stop(running);
}

TEST(spilled_arguments_outside_memory_trap) {
    expect_spill_refused(65448,
                         "the script passed the arguments of `host.seventeen` at 65448 in its "
                         "memory, where their 96 bytes run past its end at 65536");
    expect_spill_refused(0xFFFFFFF8U, "the script passed the arguments of `host.seventeen` at "
                                      "4294967288 in its memory, where their 96 bytes run past "
                                      "its end at 65536");
}

TEST(misaligned_spilled_arguments_trap) {
    expect_spill_refused(1028, "the script passed the arguments of `host.seventeen` at 1028 in its "
                               "memory, which isn't a multiple of their alignment, 8");
}

/* Loads the script <name>.wasm against hosts.stasis, which must fail with `status` and the message
 * `expected`, leaving no script behind. */
static void expect_refused(const char *name, stasis_status status, const char *expected) {
    stasis_library *library = create_library();
    const struct module_bytes module = read_module(name);
    char message[2048] = "not written";
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, &test_hosts_interface, module.bytes, module.size,
                                     message, sizeof message, &script),
                  status);
    ASSERT_STR_EQ(message, expected);
    ASSERT_TRUE(script == NULL);
    free_module(module);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* hosts.wasm imports a method and a host function of another group, which load as the others
 * do. */
TEST(loads_a_script_that_imports_host_functions) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_hosts_interface, "hosts");
    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST(refuses_host_functions_the_interface_lacks_or_declares_otherwise) {
    expect_refused("host_mismatches", STASIS_INTERFACE_MISMATCH,
                   "the script imports the host function `host.missing`, which "
                   "`test:hosts@1.0.0` lacks\n"
                   "the script imports the host function `nowhere.ping`, which "
                   "`test:hosts@1.0.0` lacks\n"
                   "the script imports the host function `host.thing.prod`, which "
                   "`test:hosts@1.0.0` lacks\n"
                   "the script imports the host function `host.gadget.poke`, which "
                   "`test:hosts@1.0.0` lacks\n"
                   "the script's host function `host.pass_u8` has another signature than in "
                   "`test:hosts@1.0.0`\n"
                   "the script's host function `host.thing.poke` has another signature than in "
                   "`test:hosts@1.0.0`\n"
                   "the script's host function `other.ping` has another signature than in "
                   "`test:hosts@1.0.0`");
}

TEST(refuses_host_function_names_that_dont_parse) {
    expect_refused("host_bad_names", STASIS_INVALID_SCRIPT,
                   "the import `Ping:2e38e77b22c314a4` from `test:hosts/host@1` doesn't parse as "
                   "the name of a host function\n"
                   "the import `ping` from `test:hosts/host@1` doesn't parse as the name of a "
                   "host function\n"
                   "the import `ping:2E38E77B22C314A4` from `test:hosts/host@1` doesn't parse as "
                   "the name of a host function\n"
                   "the import `thing.poke.again:bfc6d0f269b3c1d6` from `test:hosts/host@1` "
                   "doesn't parse as the name of a host function\n"
                   "the import `.poke:bfc6d0f269b3c1d6` from `test:hosts/host@1` doesn't parse "
                   "as the name of a host function\n"
                   "the script imports the global `count` from `test:hosts/host@1`, where a "
                   "script may import only functions");
}

TEST(refuses_host_functions_that_stasis_cant_call) {
    expect_refused("host_wrong_types", STASIS_INVALID_SCRIPT,
                   "the script imports the host function `host.pass_u8` as a function of type "
                   "(i64) -> (i32), where its signature makes it (i32) -> (i32)\n"
                   "the script imports the host function `host.ping` as a function of type () -> "
                   "(i32), where its signature makes it () -> ()\n"
                   "the script imports the host function `host.seventeen` as a function of type "
                   "(i32, i32, i32, i64, f32, f64, i32, i64, i32, i32, i32, i64, f32, f64, i32, "
                   "i32, i64) -> (i64), where its signature makes it (i32) -> (i64)");
}

/* The objects that a case's `host.ping` acts on, beside the instance that calls it. */
static struct {
    stasis_library *library;
    stasis_script *script;
    /* Another instance of hosts.wasm, started, and one only created. */
    stasis_instance *ready;
    stasis_instance *created;
    /* An instance of another script, calls.wasm, started. */
    stasis_script *calls_script;
    stasis_instance *calls;
} others;

/* Starts `others` beside `running`. */
static void start_others(const struct running *running) {
    others.library = running->library;
    others.script = running->script;
    others.ready = start_instance(running->script);
    ASSERT_INT_EQ(stasis_instance_create(running->script, NULL, &others.created), STASIS_OK);
    others.calls_script = load_script(running->library, &test_calls_interface, "calls");
    others.calls = start_instance(others.calls_script);
}

/* Destroys `others`, apart from those a case has destroyed and set to null. */
static void stop_others(void) {
    ASSERT_INT_EQ(stasis_instance_destroy(others.ready), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_destroy(others.created), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_destroy(others.calls), STASIS_OK);
    ASSERT_INT_EQ(stasis_script_destroy(others.calls_script), STASIS_OK);
    memset(&others, 0, sizeof others);
}

/* Tries to start or call the running instance and each of the others, all of which is refused, and
 * changes nothing. */
static void call_every_instance(stasis_instance *instance) {
    uint32_t result = 99;
    ASSERT_INT_EQ(test_hosts_caller_relay_u32(instance, 1, &result), STASIS_REFUSED_NESTED);
    ASSERT_UINT_EQ(result, 0);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_REFUSED_NESTED);
    result = 99;
    ASSERT_INT_EQ(test_hosts_caller_relay_u32(others.ready, 1, &result), STASIS_REFUSED_NESTED);
    ASSERT_UINT_EQ(result, 0);
    ASSERT_INT_EQ(stasis_instance_start(others.ready), STASIS_REFUSED_NESTED);
    ASSERT_INT_EQ(stasis_instance_start(others.created), STASIS_REFUSED_NESTED);
    ASSERT_INT_EQ(test_hosts_caller_relay_u32(others.created, 1, &result), STASIS_REFUSED_NESTED);
    ASSERT_INT_EQ(test_calls_caller_starts(others.calls, &result), STASIS_REFUSED_NESTED);
}

/* While an instance is running on a thread, no instance can be started or called on it, the
 * running one included. The refusals leave every instance as it was, and the call goes on. */
TEST(a_host_function_cant_call_into_any_instance) {
    const struct running running = start_hosts();
    start_others(&running);
    on_ping = call_every_instance;
    pings = 0;
    ASSERT_INT_EQ(test_hosts_caller_ping(running.instance), STASIS_OK);
    ASSERT_UINT_EQ(pings, 2);
    on_ping = NULL;

    uint32_t result = 0;
    replies.u32 = 5;
    ASSERT_INT_EQ(test_hosts_caller_relay_u32(running.instance, 1, &result), STASIS_OK);
    ASSERT_UINT_EQ(result, 5);
    ASSERT_INT_EQ(test_hosts_caller_relay_u32(others.ready, 1, &result), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_start(others.created), STASIS_OK);
    ASSERT_INT_EQ(test_calls_caller_starts(others.calls, &result), STASIS_OK);
    ASSERT_UINT_EQ(result, 1);
    stop_others();
    stop(running);
}

/* Creates, destroys and loads what doesn't run script code, the first time it's called, and fails
 * to destroy the running instance. */
static void create_and_destroy(stasis_instance *instance) {
    if (others.ready == NULL) {
        return;
    }
    stasis_instance *created = NULL;
    ASSERT_INT_EQ(stasis_instance_create(others.script, NULL, &created), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_destroy(created), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_destroy(others.ready), STASIS_OK);
    others.ready = NULL;
    ASSERT_INT_EQ(stasis_instance_destroy(others.calls), STASIS_OK);
    others.calls = NULL;
    stasis_script *loaded = load_script(others.library, &test_hosts_interface, "hosts");
    ASSERT_INT_EQ(stasis_script_destroy(loaded), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_REFUSED_RUNNING);
}

/* None of that runs script code, so a host function may do it. */
TEST(a_host_function_may_create_destroy_and_load) {
    const struct running running = start_hosts();
    start_others(&running);
    on_ping = create_and_destroy;
    pings = 0;
    ASSERT_INT_EQ(test_hosts_caller_ping(running.instance), STASIS_OK);
    ASSERT_UINT_EQ(pings, 2);
    on_ping = NULL;
    ASSERT_TRUE(others.ready == NULL);
    uint32_t result = 0;
    replies.u32 = 6;
    ASSERT_INT_EQ(test_hosts_caller_relay_u32(running.instance, 1, &result), STASIS_OK);
    ASSERT_UINT_EQ(result, 6);
    stop_others();
    stop(running);
}

/* A call that traps leaves the thread free to call any instance. */
TEST(a_trap_ends_the_running_call) {
    const struct running running = start_hosts();
    start_others(&running);
    uint64_t ignored = 0;
    ASSERT_INT_EQ(test_hosts_caller_call_seventeen_at(running.instance, 1, &ignored), STASIS_TRAP);
    uint32_t result = 0;
    replies.u32 = 7;
    ASSERT_INT_EQ(test_hosts_caller_relay_u32(others.ready, 1, &result), STASIS_OK);
    ASSERT_UINT_EQ(result, 7);
    stop_others();
    stop(running);
}

/* The size of the stack of each thread the tests run, and of each step deeper into it that a call
 * can take, which is a frame of at least that. */
enum {
    THREAD_STACK = 128 * 1024,
    STEP = 16,
};

/* Aborts the call. */
static void abort_call(stasis_instance *instance) {
    ASSERT_INT_EQ(stasis_abort(instance, "the script pinged at the wrong time"), STASIS_OK);
}

/* The index of `host.ping` among the functions of hosts.wasm, whose imports come first. */
enum { PING_IMPORT = 11 };

/* A host function that aborts returns as it would otherwise, and then the call traps, so the
 * script never calls `host.ping` a second time. The innermost frame is the host function's, which
 * the module imports, and the one that called it is the entry point's. */
TEST(a_host_function_aborts_the_call) {
    const struct running running = start_hosts();
    on_ping = abort_call;
    pings = 0;
    ASSERT_INT_EQ(test_hosts_caller_ping(running.instance), STASIS_TRAP);
    on_ping = NULL;
    ASSERT_UINT_EQ(pings, 1);
    const stasis_trap *trap = stasis_instance_trap(running.instance);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_HOST_ABORT);
    ASSERT_STR_EQ(trap->message, "the script pinged at the wrong time");
    ASSERT_SIZE_EQ(trap->frame_count, 2);
    ASSERT_UINT_EQ(trap->frames[0].function_index, PING_IMPORT);
    ASSERT_STR_EQ(trap->frames[0].function_name, NULL);
    ASSERT_UINT_EQ(trap->frames[0].module_offset, STASIS_NO_OFFSET);
    ASSERT_TRUE(trap->frames[1].function_index > PING_IMPORT);
    ASSERT_INT_EQ(test_hosts_caller_ping(running.instance), STASIS_REFUSED_TRAPPED);
    stop(running);
}

/* Aborts the call twice, from a message that changes once it's aborted. */
static void abort_call_twice(stasis_instance *instance) {
    char message[] = "first";
    ASSERT_INT_EQ(stasis_abort(instance, message), STASIS_OK);
    memcpy(message, "later", sizeof message);
    ASSERT_INT_EQ(stasis_abort(instance, "second"), STASIS_OK);
}

/* Stasis copies the message, and the first one stays. */
TEST(the_first_abort_wins) {
    const struct running running = start_hosts();
    on_ping = abort_call_twice;
    ASSERT_INT_EQ(test_hosts_caller_ping(running.instance), STASIS_TRAP);
    on_ping = NULL;
    ASSERT_STR_EQ(stasis_instance_trap(running.instance)->message, "first");
    stop(running);
}

/* Tries to abort an instance from a thread it isn't running on. */
static void abort_from_elsewhere(void *instance) {
    ASSERT_INT_EQ(stasis_abort(instance, "elsewhere"), STASIS_REFUSED_NOT_RUNNING);
}

/* Tries to abort instances that aren't running, and the running one from another thread. */
static void abort_others(stasis_instance *instance) {
    ASSERT_INT_EQ(stasis_abort(others.ready, "not running"), STASIS_REFUSED_NOT_RUNNING);
    ASSERT_INT_EQ(stasis_abort(others.created, "not started"), STASIS_REFUSED_NOT_RUNNING);
    run_on_thread(abort_from_elsewhere, instance, THREAD_STACK);
}

/* An instance that isn't running on the thread can't be aborted, so the call goes on, nor can one
 * between calls or once it has trapped. */
TEST(only_a_running_instance_can_be_aborted) {
    const struct running running = start_hosts();
    start_others(&running);
    on_ping = abort_others;
    pings = 0;
    ASSERT_INT_EQ(test_hosts_caller_ping(running.instance), STASIS_OK);
    ASSERT_UINT_EQ(pings, 2);
    on_ping = NULL;
    ASSERT_INT_EQ(stasis_abort(running.instance, "between calls"), STASIS_REFUSED_NOT_RUNNING);
    ASSERT_INT_EQ(stasis_abort(others.ready, "between calls"), STASIS_REFUSED_NOT_RUNNING);
    ASSERT_TRUE(stasis_instance_trap(running.instance) == NULL);
    uint64_t ignored = 0;
    ASSERT_INT_EQ(test_hosts_caller_call_seventeen_at(running.instance, 1, &ignored), STASIS_TRAP);
    ASSERT_INT_EQ(stasis_abort(running.instance, "trapped"), STASIS_REFUSED_NOT_RUNNING);
    stop_others();
    stop(running);
}

TEST(aborting_takes_an_instance_and_a_message) {
    const struct running running = start_hosts();
    ASSERT_INT_EQ(stasis_abort(NULL, "no instance"), STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_abort(running.instance, NULL), STASIS_INVALID_ARGUMENT);
    stop(running);
}

/* The lowest address of the stack of the thread that calls deep into it, and the least of the
 * stack that a host function has had free. */
static uintptr_t thread_stack_end;
static size_t least_free_stack = SIZE_MAX;

/* Keeps a function's frame on the thread's stack, where AddressSanitizer would otherwise move it to
 * a heap of its own, to catch uses of it once the function has returned. */
#ifdef __GNUC__
#define ON_THE_STACK __attribute__((no_sanitize_address))
#else
#define ON_THE_STACK
#endif

/* Notes how much of the stack is free below this frame, which is no higher than the host
 * function's. */
ON_THE_STACK static void note_free_stack(stasis_instance *instance) {
    (void)instance;
    const volatile unsigned char here = 0;
    const size_t free_stack = (size_t)((uintptr_t)&here - thread_stack_end);
    if (free_stack < least_free_stack) {
        least_free_stack = free_stack;
    }
}

/* Calls `ping` with `steps` frames of the thread's stack used up first. */
/* NOLINTNEXTLINE(misc-no-recursion): each step deeper is a frame, which is the point */
ON_THE_STACK static stasis_status ping_at_depth(stasis_instance *instance, unsigned steps) {
    volatile unsigned char frame[STEP];
    frame[0] = (unsigned char)steps;
    const stasis_status status =
        steps == 0 ? test_hosts_caller_ping(instance) : ping_at_depth(instance, steps - 1);
    frame[STEP - 1] = frame[0];
    return status;
}

/* How calls deeper and deeper into the thread's stack ended: the deepest that reached the host
 * function, the first that trapped before the host function ran, and the first that trapped as it
 * entered the script, with no frames. */
struct descent {
    stasis_script *script;
    unsigned deepest_call;
    unsigned first_trap;
    unsigned first_trap_on_entry;
};

/* Calls `ping` deeper and deeper into the thread's stack, in a new instance after each trap, until
 * a call traps as soon as it enters the script. Each trap is for lack of stack. */
static void descend(void *argument) {
    struct descent *descent = argument;
    thread_stack_end = stack_end();
    stasis_instance *instance = start_instance(descent->script);
    for (unsigned steps = 0; steps < THREAD_STACK / STEP; steps++) {
        pings = 0;
        const stasis_status status = ping_at_depth(instance, steps);
        if (status == STASIS_OK) {
            ASSERT_UINT_EQ(pings, 2);
            descent->deepest_call = steps;
            continue;
        }
        ASSERT_INT_EQ(status, STASIS_TRAP);
        ASSERT_UINT_EQ(pings, 0);
        const stasis_trap *trap = stasis_instance_trap(instance);
        ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_STACK_EXHAUSTED);
        ASSERT_STR_EQ(trap->message, "native stack overflow");
        if (trap->frame_count == 0) {
            descent->first_trap_on_entry = steps;
            break;
        }
        if (descent->first_trap == 0) {
            descent->first_trap = steps;
        }
        ASSERT_SIZE_EQ(trap->frame_count, 1);
        ASSERT_TRUE(trap->frames[0].function_index > PING_IMPORT);
        ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_OK);
        instance = start_instance(descent->script);
    }
    ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_OK);
}

/* A call that comes too near the end of the native stack traps as stack_exhausted, and the program
 * goes on. Before a host function runs, WAMR checks that the stack it's sure of is there, so the
 * deepest call that reaches it still has that much free. The first call deeper traps there, having
 * run the script, and later ones trap at once. */
TEST(a_host_function_is_sure_of_its_stack) {
    stasis_library *library = create_library();
    struct descent descent;
    memset(&descent, 0, sizeof descent);
    descent.script = load_script(library, &test_hosts_interface, "hosts");
    on_ping = note_free_stack;
    run_on_thread(descend, &descent, THREAD_STACK);
    on_ping = NULL;
    ASSERT_TRUE(descent.first_trap > 0);
    ASSERT_UINT_EQ(descent.deepest_call + 1, descent.first_trap);
    ASSERT_TRUE(descent.first_trap_on_entry > descent.first_trap);
    ASSERT_TRUE(least_free_stack >= STASIS_HOST_FUNCTION_STACK);
    ASSERT_INT_EQ(stasis_script_destroy(descent.script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

TEST_LIST(TEST_CASE(a_script_calls_a_host_function), TEST_CASE(calls_a_host_function_of_each_group),
          TEST_CASE(a_method_called_on_handle_0_traps),
          TEST_CASE(passes_unsigned_integers_both_ways),
          TEST_CASE(passes_signed_integers_both_ways),
          TEST_CASE(passes_floats_bit_for_bit_both_ways), TEST_CASE(passes_bools_both_ways),
          TEST_CASE(narrows_arguments_to_their_type), TEST_CASE(widens_narrow_results),
          TEST_CASE(passes_sixteen_arguments_as_core_parameters),
          TEST_CASE(reads_spilled_arguments_from_the_scripts_memory),
          TEST_CASE(reads_spilled_arguments_that_end_where_memory_does),
          TEST_CASE(spilled_arguments_outside_memory_trap),
          TEST_CASE(misaligned_spilled_arguments_trap),
          TEST_CASE(loads_a_script_that_imports_host_functions),
          TEST_CASE(refuses_host_functions_the_interface_lacks_or_declares_otherwise),
          TEST_CASE(refuses_host_function_names_that_dont_parse),
          TEST_CASE(refuses_host_functions_that_stasis_cant_call),
          TEST_CASE(a_host_function_cant_call_into_any_instance),
          TEST_CASE(a_host_function_may_create_destroy_and_load),
          TEST_CASE(a_trap_ends_the_running_call), TEST_CASE(a_host_function_aborts_the_call),
          TEST_CASE(the_first_abort_wins), TEST_CASE(only_a_running_instance_can_be_aborted),
          TEST_CASE(aborting_takes_an_instance_and_a_message),
          TEST_CASE(a_host_function_is_sure_of_its_stack));
