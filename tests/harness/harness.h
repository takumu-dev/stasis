/* Stasis's test harness.
 *
 * A test program defines its cases with TEST, names each of them once in TEST_LIST, and links the
 * harness, which provides main:
 *
 *     TEST(adds_small_numbers) {
 *         ASSERT_INT_EQ(1 + 1, 2);
 *     }
 *
 *     TEST_LIST(TEST_CASE(adds_small_numbers));
 *
 * With no arguments, the program runs every case in turn. With --list, it prints each case's name
 * on a line of its own, and with a case's exact name, it runs that case alone. It exits with 0 when
 * every case it ran passed, 1 when one failed, and 2 for a bad command line, a name that matches no
 * case included.
 *
 * A failed assertion prints where it failed and, for a comparison, both values, then ends the
 * program, from whichever function or thread of the case it's in. So a case stops at its first
 * failure, and a run of every case at the first case that fails.
 */

#ifndef STASIS_HARNESS_H
#define STASIS_HARNESS_H

#include <stddef.h>
#include <stdint.h>

/* One case of a test program: its name and the function that runs it. */
struct harness_case {
    const char *name;
    void (*run)(void);
};

/* Every case of the test program, in the order it runs them, which TEST_LIST defines. */
extern const struct harness_case harness_cases[];
extern const size_t harness_case_count;

/* Defines the case `name`, whose body follows. */
#define TEST(name) static void test_##name(void)

/* The entry of TEST_LIST for the case `name`. */
#define TEST_CASE(name) {#name, test_##name}

/* Lists the test program's cases, each given by TEST_CASE. */
#define TEST_LIST(...)                                                                             \
    const struct harness_case harness_cases[] = {__VA_ARGS__};                                     \
    const size_t harness_case_count = sizeof harness_cases / sizeof harness_cases[0]

/* Fails unless `condition` holds. */
#define ASSERT_TRUE(condition)                                                                     \
    ((condition) ? (void)0 : harness_fail(__FILE__, __LINE__, "ASSERT_TRUE(" #condition ")"))

/* Fails unless two signed integers are equal. */
#define ASSERT_INT_EQ(actual, expected)                                                            \
    harness_assert_int(__FILE__, __LINE__, "ASSERT_INT_EQ(" #actual ", " #expected ")", (actual),  \
                       (expected))

/* Fails unless two unsigned integers are equal. */
#define ASSERT_UINT_EQ(actual, expected)                                                           \
    harness_assert_uint(__FILE__, __LINE__, "ASSERT_UINT_EQ(" #actual ", " #expected ")",          \
                        (actual), (expected))

/* Fails unless two sizes are equal, compared as unsigned integers. */
#define ASSERT_SIZE_EQ(actual, expected)                                                           \
    harness_assert_uint(__FILE__, __LINE__, "ASSERT_SIZE_EQ(" #actual ", " #expected ")",          \
                        (actual), (expected))

/* Fails unless two strings are equal, or both null. */
#define ASSERT_STR_EQ(actual, expected)                                                            \
    harness_assert_str(__FILE__, __LINE__, "ASSERT_STR_EQ(" #actual ", " #expected ")", (actual),  \
                       (expected))

/* Fails unless the `size` bytes at `actual` and at `expected` are equal. */
#define ASSERT_MEM_EQ(actual, expected, size)                                                      \
    harness_assert_mem(__FILE__, __LINE__, "ASSERT_MEM_EQ(" #actual ", " #expected ", " #size ")", \
                       (actual), (expected), (size))

/* Fails unless two floats are the same value: NaN equals NaN, and -0 doesn't equal 0. */
#define ASSERT_FLOAT_EQ(actual, expected)                                                          \
    harness_assert_float(__FILE__, __LINE__, "ASSERT_FLOAT_EQ(" #actual ", " #expected ")",        \
                         (actual), (expected))

/* Fails unless two doubles are the same value, as ASSERT_FLOAT_EQ compares floats. */
#define ASSERT_DOUBLE_EQ(actual, expected)                                                         \
    harness_assert_double(__FILE__, __LINE__, "ASSERT_DOUBLE_EQ(" #actual ", " #expected ")",      \
                          (actual), (expected))

/* What the assertions call. Each takes the file and line of the assertion and its text. */

_Noreturn void harness_fail(const char *file, int line, const char *assertion);
void harness_assert_int(const char *file, int line, const char *assertion, intmax_t actual,
                        intmax_t expected);
void harness_assert_uint(const char *file, int line, const char *assertion, uintmax_t actual,
                         uintmax_t expected);
void harness_assert_str(const char *file, int line, const char *assertion, const char *actual,
                        const char *expected);
void harness_assert_mem(const char *file, int line, const char *assertion, const void *actual,
                        const void *expected, size_t size);
void harness_assert_float(const char *file, int line, const char *assertion, float actual,
                          float expected);
void harness_assert_double(const char *file, int line, const char *assertion, double actual,
                           double expected);

#endif
