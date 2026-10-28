/* Every scalar type, crossing to an entry point and back as the canonical ABI lowers and lifts it:
 * integers narrower than 32 bits widen as they go in and narrow as they come out, and floats keep
 * every bit. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_calls.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* An instance of the script calls.wat, started, with its script and library. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

static struct running start_calls(void) {
    struct running running;
    running.library = create_library();
    running.script = load_script(running.library, &test_calls_interface, "calls");
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

static uint32_t bits_of_f32(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static double f64_of(uint64_t bits) {
    double value = 0.0;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static uint64_t bits_of_f64(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

TEST(echoes_unsigned_integers) {
    const struct running running = start_calls();
    stasis_instance *instance = running.instance;
    uint8_t u8 = 0;
    ASSERT_INT_EQ(test_calls_caller_echo_u8(instance, 255, &u8), STASIS_OK);
    ASSERT_UINT_EQ(u8, 255);
    ASSERT_INT_EQ(test_calls_caller_echo_u8(instance, 0, &u8), STASIS_OK);
    ASSERT_UINT_EQ(u8, 0);
    uint16_t u16 = 0;
    ASSERT_INT_EQ(test_calls_caller_echo_u16(instance, 65535, &u16), STASIS_OK);
    ASSERT_UINT_EQ(u16, 65535);
    uint32_t u32 = 0;
    ASSERT_INT_EQ(test_calls_caller_echo_u32(instance, 0xFFFFFFFFU, &u32), STASIS_OK);
    ASSERT_UINT_EQ(u32, 0xFFFFFFFFU);
    uint64_t u64 = 0;
    ASSERT_INT_EQ(test_calls_caller_echo_u64(instance, 0xFFFFFFFFFFFFFFFFU, &u64), STASIS_OK);
    ASSERT_UINT_EQ(u64, 0xFFFFFFFFFFFFFFFFU);
    ASSERT_INT_EQ(test_calls_caller_echo_u64(instance, 0x8000000000000001U, &u64), STASIS_OK);
    ASSERT_UINT_EQ(u64, 0x8000000000000001U);
    stop(running);
}

TEST(echoes_signed_integers) {
    const struct running running = start_calls();
    stasis_instance *instance = running.instance;
    int8_t s8 = 0;
    ASSERT_INT_EQ(test_calls_caller_echo_s8(instance, -128, &s8), STASIS_OK);
    ASSERT_INT_EQ(s8, -128);
    ASSERT_INT_EQ(test_calls_caller_echo_s8(instance, 127, &s8), STASIS_OK);
    ASSERT_INT_EQ(s8, 127);
    int16_t s16 = 0;
    ASSERT_INT_EQ(test_calls_caller_echo_s16(instance, -32768, &s16), STASIS_OK);
    ASSERT_INT_EQ(s16, -32768);
    int32_t s32 = 0;
    ASSERT_INT_EQ(test_calls_caller_echo_s32(instance, INT32_MIN, &s32), STASIS_OK);
    ASSERT_INT_EQ(s32, INT32_MIN);
    int64_t s64 = 0;
    ASSERT_INT_EQ(test_calls_caller_echo_s64(instance, INT64_MIN, &s64), STASIS_OK);
    ASSERT_INT_EQ(s64, INT64_MIN);
    ASSERT_INT_EQ(test_calls_caller_echo_s64(instance, -2, &s64), STASIS_OK);
    ASSERT_INT_EQ(s64, -2);
    stop(running);
}

/* Negative zero, infinity, a subnormal and a NaN with a payload all come back as they went. */
TEST(echoes_floats_bit_for_bit) {
    const struct running running = start_calls();
    stasis_instance *instance = running.instance;
    const uint32_t f32_bits[] = {0x80000000U, 0x7F800000U, 0x00000001U, 0x7FC12345U, 0x3FC00000U};
    for (size_t i = 0; i < sizeof f32_bits / sizeof f32_bits[0]; i++) {
        float f32 = 0.0F;
        ASSERT_INT_EQ(test_calls_caller_echo_f32(instance, f32_of(f32_bits[i]), &f32), STASIS_OK);
        ASSERT_UINT_EQ(bits_of_f32(f32), f32_bits[i]);
    }
    const uint64_t f64_bits[] = {0x8000000000000000U, 0xFFF0000000000000U, 0x0000000000000001U,
                                 0x7FF8000000012345U, 0xC002000000000000U};
    for (size_t i = 0; i < sizeof f64_bits / sizeof f64_bits[0]; i++) {
        double f64 = 0.0;
        ASSERT_INT_EQ(test_calls_caller_echo_f64(instance, f64_of(f64_bits[i]), &f64), STASIS_OK);
        ASSERT_UINT_EQ(bits_of_f64(f64), f64_bits[i]);
    }
    stop(running);
}

TEST(echoes_bools) {
    const struct running running = start_calls();
    bool echoed = false;
    ASSERT_INT_EQ(test_calls_caller_echo_bool(running.instance, true, &echoed), STASIS_OK);
    ASSERT_TRUE(echoed);
    ASSERT_INT_EQ(test_calls_caller_echo_bool(running.instance, false, &echoed), STASIS_OK);
    ASSERT_TRUE(!echoed);
    stop(running);
}

/* The script sees an unsigned argument extended with zeros, a signed one with its sign, and a bool
 * as 0 or 1. */
TEST(widens_narrow_arguments) {
    const struct running running = start_calls();
    stasis_instance *instance = running.instance;
    uint32_t unsigned_value = 0;
    ASSERT_INT_EQ(test_calls_caller_extend_u8(instance, 255, &unsigned_value), STASIS_OK);
    ASSERT_UINT_EQ(unsigned_value, 255);
    ASSERT_INT_EQ(test_calls_caller_extend_u16(instance, 65535, &unsigned_value), STASIS_OK);
    ASSERT_UINT_EQ(unsigned_value, 65535);
    int32_t signed_value = 0;
    ASSERT_INT_EQ(test_calls_caller_extend_s8(instance, -1, &signed_value), STASIS_OK);
    ASSERT_INT_EQ(signed_value, -1);
    ASSERT_INT_EQ(test_calls_caller_extend_s8(instance, -128, &signed_value), STASIS_OK);
    ASSERT_INT_EQ(signed_value, -128);
    ASSERT_INT_EQ(test_calls_caller_extend_s16(instance, -32768, &signed_value), STASIS_OK);
    ASSERT_INT_EQ(signed_value, -32768);
    ASSERT_INT_EQ(test_calls_caller_extend_bool(instance, true, &unsigned_value), STASIS_OK);
    ASSERT_UINT_EQ(unsigned_value, 1);
    ASSERT_INT_EQ(test_calls_caller_extend_bool(instance, false, &unsigned_value), STASIS_OK);
    ASSERT_UINT_EQ(unsigned_value, 0);
    stop(running);
}

/* A result narrower than 32 bits keeps the low bits of what the script returns, and a bool is
 * true unless the script returns 0. */
TEST(narrows_results_to_their_type) {
    const struct running running = start_calls();
    stasis_instance *instance = running.instance;
    uint8_t u8 = 0;
    ASSERT_INT_EQ(test_calls_caller_wrap_u8(instance, 0x1FF, &u8), STASIS_OK);
    ASSERT_UINT_EQ(u8, 0xFF);
    ASSERT_INT_EQ(test_calls_caller_wrap_u8(instance, 0xFFFFFF00U, &u8), STASIS_OK);
    ASSERT_UINT_EQ(u8, 0);
    uint16_t u16 = 0;
    ASSERT_INT_EQ(test_calls_caller_wrap_u16(instance, 0x12345678, &u16), STASIS_OK);
    ASSERT_UINT_EQ(u16, 0x5678);
    int8_t s8 = 0;
    ASSERT_INT_EQ(test_calls_caller_wrap_s8(instance, 0x80, &s8), STASIS_OK);
    ASSERT_INT_EQ(s8, -128);
    ASSERT_INT_EQ(test_calls_caller_wrap_s8(instance, 0x17F, &s8), STASIS_OK);
    ASSERT_INT_EQ(s8, 127);
    ASSERT_INT_EQ(test_calls_caller_wrap_s8(instance, 0xFFFFFFFFU, &s8), STASIS_OK);
    ASSERT_INT_EQ(s8, -1);
    int16_t s16 = 0;
    ASSERT_INT_EQ(test_calls_caller_wrap_s16(instance, 0x8000, &s16), STASIS_OK);
    ASSERT_INT_EQ(s16, -32768);
    ASSERT_INT_EQ(test_calls_caller_wrap_s16(instance, 0x1FFFF, &s16), STASIS_OK);
    ASSERT_INT_EQ(s16, -1);
    bool flag = false;
    ASSERT_INT_EQ(test_calls_caller_wrap_bool(instance, 2, &flag), STASIS_OK);
    ASSERT_TRUE(flag);
    ASSERT_INT_EQ(test_calls_caller_wrap_bool(instance, 0x100, &flag), STASIS_OK);
    ASSERT_TRUE(flag);
    ASSERT_INT_EQ(test_calls_caller_wrap_bool(instance, 0, &flag), STASIS_OK);
    ASSERT_TRUE(!flag);
    stop(running);
}

/* Arguments of every size reach the script in order, each in full. The expected sum was worked out
 * apart from Stasis. */
TEST(passes_mixed_arguments_in_order) {
    const struct running running = start_calls();
    uint64_t sum = 0;
    ASSERT_INT_EQ(test_calls_caller_mix(running.instance, 200, -2, 4000000000U, 0x123456789ABCDEF0U,
                                        1.5F, -2.25, true, -5, &sum),
                  STASIS_OK);
    ASSERT_UINT_EQ(sum, 0x3F885D53A081E104U);
    stop(running);
}

TEST_LIST(TEST_CASE(echoes_unsigned_integers), TEST_CASE(echoes_signed_integers),
          TEST_CASE(echoes_floats_bit_for_bit), TEST_CASE(echoes_bools),
          TEST_CASE(widens_narrow_arguments), TEST_CASE(narrows_results_to_their_type),
          TEST_CASE(passes_mixed_arguments_in_order));
