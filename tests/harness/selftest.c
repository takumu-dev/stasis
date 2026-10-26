/* The harness's own test program. Its cases pass or fail on purpose: the harness's checks in
 * tests/CMakeLists.txt run it and look at how it exits and what it prints. */

#include "harness.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* Every assertion, given values it accepts. */
TEST(passes) {
    ASSERT_TRUE(1 + 1 == 2);
    ASSERT_INT_EQ(INTMAX_MIN, INTMAX_MIN);
    ASSERT_UINT_EQ(UINTMAX_MAX, UINTMAX_MAX);
    ASSERT_SIZE_EQ(SIZE_MAX, SIZE_MAX);
    ASSERT_STR_EQ("stasis", "stasis");
    ASSERT_STR_EQ(NULL, NULL);
    ASSERT_MEM_EQ("\x01\x02\x03", "\x01\x02\x03", 3);
    ASSERT_MEM_EQ(NULL, NULL, 0);
    ASSERT_FLOAT_EQ(0.5F, 0.5F);
    ASSERT_FLOAT_EQ(NAN, NAN);
    ASSERT_DOUBLE_EQ(-0.0, -0.0);
    ASSERT_DOUBLE_EQ((double)NAN, (double)NAN);
}

TEST(fails_true) { ASSERT_TRUE(1 + 1 == 3); }

/* Prints a line after its failed assertion, which never runs. */
TEST(fails_int) {
    ASSERT_INT_EQ(-2, 3);
    puts("after the failure");
}

/* Fails in a function the case calls. */
static void check_uint(uintmax_t value) { ASSERT_UINT_EQ(value, 0); }

TEST(fails_uint) {
    check_uint(UINTMAX_MAX);
    puts("after the failure");
}

TEST(fails_size) { ASSERT_SIZE_EQ(SIZE_MAX, 1); }

TEST(fails_str) { ASSERT_STR_EQ("tab\there", "quote\"there"); }

TEST(fails_str_null) { ASSERT_STR_EQ(NULL, "stasis"); }

TEST(fails_mem) { ASSERT_MEM_EQ("\x00\x01\x02\x03", "\x00\x01\xff\x03", 4); }

/* Differs at byte 40 of 64, so only the bytes around it are printed. */
TEST(fails_long_mem) {
    unsigned char actual[64] = {0};
    unsigned char expected[64] = {0};
    expected[40] = 0xab;
    ASSERT_MEM_EQ(actual, expected, sizeof actual);
}

TEST(fails_float) { ASSERT_FLOAT_EQ(0.0F, -0.0F); }

TEST(fails_double) { ASSERT_DOUBLE_EQ(0.1, (double)NAN); }

TEST_LIST(TEST_CASE(passes), TEST_CASE(fails_true), TEST_CASE(fails_int), TEST_CASE(fails_uint),
          TEST_CASE(fails_size), TEST_CASE(fails_str), TEST_CASE(fails_str_null),
          TEST_CASE(fails_mem), TEST_CASE(fails_long_mem), TEST_CASE(fails_float),
          TEST_CASE(fails_double));
