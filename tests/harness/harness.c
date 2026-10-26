#include "harness.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The exit status for a bad command line. */
enum { EXIT_USAGE = 2 };

/* The most bytes a failed ASSERT_MEM_EQ prints of each block. */
enum { MEM_WINDOW = 32 };

/* The case that's running, which a failed assertion names. */
static const struct harness_case *running_case;

/* Prints the line that starts every failure: where the assertion is, and its text. */
static void print_failure(const char *file, int line, const char *assertion) {
    printf("%s:%d: %s failed\n", file, line, assertion);
}

/* Ends the program after a failed assertion, naming the running case. It exits at once, since the
 * case's memory and threads may be in any state, and a sanitiser's leak check would only add noise
 * to the failure. */
_Noreturn static void end_case(void) {
    printf("FAIL %s\n", running_case->name);
    fflush(stdout);
    _Exit(EXIT_FAILURE);
}

_Noreturn void harness_fail(const char *file, int line, const char *assertion) {
    print_failure(file, line, assertion);
    end_case();
}

void harness_assert_int(const char *file, int line, const char *assertion, intmax_t actual,
                        intmax_t expected) {
    if (actual == expected) {
        return;
    }
    print_failure(file, line, assertion);
    printf("  actual:   %jd\n  expected: %jd\n", actual, expected);
    end_case();
}

void harness_assert_uint(const char *file, int line, const char *assertion, uintmax_t actual,
                         uintmax_t expected) {
    if (actual == expected) {
        return;
    }
    print_failure(file, line, assertion);
    printf("  actual:   %ju\n  expected: %ju\n", actual, expected);
    end_case();
}

/* Prints a string as a C string literal would spell it, or NULL. */
static void print_string(const char *string) {
    if (string == NULL) {
        fputs("NULL", stdout);
        return;
    }
    putchar('"');
    for (const unsigned char *c = (const unsigned char *)string; *c != '\0'; c++) {
        switch (*c) {
        case '"':
            fputs("\\\"", stdout);
            break;
        case '\\':
            fputs("\\\\", stdout);
            break;
        case '\n':
            fputs("\\n", stdout);
            break;
        case '\r':
            fputs("\\r", stdout);
            break;
        case '\t':
            fputs("\\t", stdout);
            break;
        default:
            if (*c < 0x20 || *c >= 0x7f) {
                printf("\\x%02x", *c);
            } else {
                putchar(*c);
            }
        }
    }
    putchar('"');
}

void harness_assert_str(const char *file, int line, const char *assertion, const char *actual,
                        const char *expected) {
    if (actual == expected ||
        (actual != NULL && expected != NULL && strcmp(actual, expected) == 0)) {
        return;
    }
    print_failure(file, line, assertion);
    fputs("  actual:   ", stdout);
    print_string(actual);
    fputs("\n  expected: ", stdout);
    print_string(expected);
    putchar('\n');
    end_case();
}

/* Prints the bytes from `start` to `end` in hexadecimal, separated by spaces. */
static void print_bytes(const unsigned char *bytes, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        printf(i == start ? "%02x" : " %02x", bytes[i]);
    }
    putchar('\n');
}

void harness_assert_mem(const char *file, int line, const char *assertion, const void *actual,
                        const void *expected, size_t size) {
    if (size == 0 || memcmp(actual, expected, size) == 0) {
        return;
    }
    const unsigned char *actual_bytes = actual;
    const unsigned char *expected_bytes = expected;
    size_t difference = 0;
    while (actual_bytes[difference] == expected_bytes[difference]) {
        difference++;
    }
    /* A block that fits is printed whole, and a longer one from the row of 16 bytes that holds the
     * first difference. */
    const size_t start = size <= MEM_WINDOW ? 0 : difference / 16 * 16;
    const size_t end = size - start <= MEM_WINDOW ? size : start + MEM_WINDOW;
    print_failure(file, line, assertion);
    printf("  first difference at byte %zu of %zu; bytes %zu to %zu are:\n", difference, size,
           start, end - 1);
    fputs("  actual:   ", stdout);
    print_bytes(actual_bytes, start, end);
    fputs("  expected: ", stdout);
    print_bytes(expected_bytes, start, end);
    end_case();
}

/* Whether two floating-point values are the same: NaN is the same as NaN, and -0 isn't 0. */
static bool same_value(double actual, double expected) {
    if (isnan(actual) || isnan(expected)) {
        return isnan(actual) != 0 && isnan(expected) != 0;
    }
    if (actual != expected) {
        return false;
    }
    /* 0 == -0, so only their signs tell them apart. */
    return (signbit(actual) != 0) == (signbit(expected) != 0);
}

/* Prints a floating-point value with enough digits to tell it from any other. Every C library
 * spells NaN its own way, so this spells it nan. */
static void print_value(const char *label, double value, int digits) {
    if (isnan(value)) {
        printf("  %s nan\n", label);
    } else {
        printf("  %s %.*g\n", label, digits, value);
    }
}

void harness_assert_float(const char *file, int line, const char *assertion, float actual,
                          float expected) {
    if (same_value((double)actual, (double)expected)) {
        return;
    }
    print_failure(file, line, assertion);
    print_value("actual:  ", (double)actual, 9);
    print_value("expected:", (double)expected, 9);
    end_case();
}

void harness_assert_double(const char *file, int line, const char *assertion, double actual,
                           double expected) {
    if (same_value(actual, expected)) {
        return;
    }
    print_failure(file, line, assertion);
    print_value("actual:  ", actual, 17);
    print_value("expected:", expected, 17);
    end_case();
}

/* Runs one case. If it fails, the program ends there. */
static void run_case(const struct harness_case *test_case) {
    running_case = test_case;
    test_case->run();
    printf("PASS %s\n", test_case->name);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc == 1) {
        for (size_t i = 0; i < harness_case_count; i++) {
            run_case(&harness_cases[i]);
        }
        printf("%zu passed\n", harness_case_count);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (size_t i = 0; i < harness_case_count; i++) {
            puts(harness_cases[i].name);
        }
        return EXIT_SUCCESS;
    }
    if (argc == 2 && argv[1][0] != '-') {
        for (size_t i = 0; i < harness_case_count; i++) {
            if (strcmp(argv[1], harness_cases[i].name) == 0) {
                run_case(&harness_cases[i]);
                return EXIT_SUCCESS;
            }
        }
        fprintf(stderr, "%s: no test case is named %s\n", argv[0], argv[1]);
        return EXIT_USAGE;
    }
    fprintf(stderr, "usage: %s [--list | <case>]\n", argv[0]);
    return EXIT_USAGE;
}
