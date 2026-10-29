/* What a C script's standard library does through the system interface, with the library's hooks
 * and without them: stdio's buffering, a failed assert, time, arc4random, getenv, exit and
 * fopen, as c_library.wasm uses them. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_c_library.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Everything the output hook has been given, by stream: 1 for stdout, and 2 for stderr. */
static char output[3][256];
static size_t output_size[3];

static void record_output(stasis_instance *instance, void *user_data, stasis_stream stream,
                          const char *bytes, size_t size) {
    (void)instance;
    (void)user_data;
    ASSERT_TRUE(stream == STASIS_STREAM_STDOUT || stream == STASIS_STREAM_STDERR);
    ASSERT_TRUE(size <= sizeof output[0] - 1 - output_size[stream]);
    memcpy(output[stream] + output_size[stream], bytes, size);
    output_size[stream] += size;
    output[stream][output_size[stream]] = '\0';
}

/* The time the clock hook answers, in nanoseconds: half a second after 1700000000 seconds. */
static const uint64_t time_answered = 1700000000500000000U;

static bool read_clock(stasis_instance *instance, void *user_data, uint32_t clock,
                       stasis_clock_reading reading, uint64_t precision, uint64_t *nanoseconds) {
    (void)instance;
    (void)user_data;
    (void)precision;
    ASSERT_UINT_EQ(clock, 0);
    ASSERT_INT_EQ(reading, STASIS_CLOCK_TIME);
    *nanoseconds = time_answered;
    return true;
}

/* How many bytes the entropy hook has been asked for, which it fills with 7s. */
static size_t entropy_size;

static bool draw_entropy(stasis_instance *instance, void *user_data, void *buffer, size_t size) {
    (void)instance;
    (void)user_data;
    memset(buffer, 7, size);
    entropy_size += size;
    return true;
}

/* An instance of c_library.wasm, started, with its script and its library. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

/* Starts an instance of c_library.wasm in a library of its own, with an output hook, and the clock
 * and entropy hooks too when `hooked` is set, having forgotten all output. */
static struct running start_script(bool hooked) {
    memset(output, 0, sizeof output);
    memset(output_size, 0, sizeof output_size);
    entropy_size = 0;
    stasis_library_settings settings;
    memset(&settings, 0, sizeof settings);
    settings.output = record_output;
    if (hooked) {
        settings.clock = read_clock;
        settings.entropy = draw_entropy;
    }
    struct running running;
    ASSERT_INT_EQ(stasis_library_create(&settings, &running.library), STASIS_OK);
    running.script = load_script(running.library, &test_c_library_interface, "c_library");
    running.instance = start_instance(running.script);
    return running;
}

static void stop(struct running running) {
    destroy_all(running.instance, running.script, running.library);
}

/* stdout is a terminal, as wasi-libc's isatty finds, so printf buffers it by the line: a line
 * reaches the output hook once it's whole, and a line without its newline waits in the script's
 * memory until a later call ends it. */
TEST(printf_buffers_stdout_by_the_line) {
    const struct running running = start_script(false);
    ASSERT_INT_EQ(test_c_library_user_print_line(running.instance), STASIS_OK);
    ASSERT_STR_EQ(output[STASIS_STREAM_STDOUT], "hello, world\n");
    ASSERT_INT_EQ(test_c_library_user_print_partial(running.instance), STASIS_OK);
    ASSERT_STR_EQ(output[STASIS_STREAM_STDOUT], "hello, world\n");
    ASSERT_INT_EQ(test_c_library_user_print_line(running.instance), STASIS_OK);
    ASSERT_STR_EQ(output[STASIS_STREAM_STDOUT], "hello, world\npartialhello, world\n");
    ASSERT_SIZE_EQ(output_size[STASIS_STREAM_STDERR], 0);
    stop(running);
}

/* stderr isn't buffered, so what's printed to it reaches the output hook at once. */
TEST(stderr_is_written_at_once) {
    const struct running running = start_script(false);
    ASSERT_INT_EQ(test_c_library_user_print_warning(running.instance), STASIS_OK);
    ASSERT_STR_EQ(output[STASIS_STREAM_STDERR], "low health");
    ASSERT_SIZE_EQ(output_size[STASIS_STREAM_STDOUT], 0);
    stop(running);
}

/* A failed assert writes its message to stderr, then traps as unreachable without flushing stdout,
 * so a line that was waiting is lost with the instance. */
TEST(a_failed_assert_writes_to_stderr_and_traps) {
    const struct running running = start_script(false);
    ASSERT_INT_EQ(test_c_library_user_print_partial(running.instance), STASIS_OK);
    ASSERT_INT_EQ(test_c_library_user_fail_assert(running.instance), STASIS_TRAP);
    ASSERT_INT_EQ(stasis_instance_trap(running.instance)->kind, STASIS_TRAP_KIND_UNREACHABLE);
    const char expected[] = "Assertion failed: health > 0 (";
    ASSERT_TRUE(output_size[STASIS_STREAM_STDERR] > sizeof expected - 1);
    ASSERT_MEM_EQ(output[STASIS_STREAM_STDERR], expected, sizeof expected - 1);
    ASSERT_SIZE_EQ(output_size[STASIS_STREAM_STDOUT], 0);
    stop(running);
}

/* time reads real time from the clock hook, in whole seconds, and is 0 without one. */
TEST(time_reads_the_clock_hook_and_is_0_without_one) {
    struct running running = start_script(true);
    int64_t seconds = 0;
    ASSERT_INT_EQ(test_c_library_user_read_time(running.instance, &seconds), STASIS_OK);
    ASSERT_INT_EQ(seconds, 1700000000);
    stop(running);
    running = start_script(false);
    seconds = -1;
    ASSERT_INT_EQ(test_c_library_user_read_time(running.instance, &seconds), STASIS_OK);
    ASSERT_INT_EQ(seconds, 0);
    stop(running);
}

/* arc4random draws its key from the entropy hook, and traps without one. */
TEST(arc4random_draws_from_the_entropy_hook_and_traps_without_one) {
    struct running running = start_script(true);
    uint32_t drawn = 0;
    ASSERT_INT_EQ(test_c_library_user_draw_random(running.instance, &drawn), STASIS_OK);
    ASSERT_TRUE(entropy_size > 0);
    stop(running);
    running = start_script(false);
    ASSERT_INT_EQ(test_c_library_user_draw_random(running.instance, &drawn), STASIS_TRAP);
    ASSERT_INT_EQ(stasis_instance_trap(running.instance)->kind, STASIS_TRAP_KIND_UNREACHABLE);
    stop(running);
}

/* The environment is empty, whatever the host's, so getenv finds nothing. */
TEST(getenv_finds_nothing) {
    const struct running running = start_script(true);
    bool found = true;
    ASSERT_INT_EQ(test_c_library_user_has_home(running.instance, &found), STASIS_OK);
    ASSERT_TRUE(!found);
    stop(running);
}

/* exit flushes stdout, so a line that was waiting reaches the output hook, and then the call traps
 * as exit, even with a status of 0. */
TEST(exit_flushes_stdout_and_traps_as_exit) {
    const struct running running = start_script(false);
    ASSERT_INT_EQ(test_c_library_user_print_partial(running.instance), STASIS_OK);
    ASSERT_INT_EQ(test_c_library_user_exit_zero(running.instance), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(running.instance);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_EXIT);
    ASSERT_STR_EQ(trap->message, "the script exited with status 0");
    ASSERT_STR_EQ(output[STASIS_STREAM_STDOUT], "partial");
    stop(running);
}

/* There are no files, so fopen fails. */
TEST(fopen_fails) {
    const struct running running = start_script(true);
    bool opened = true;
    ASSERT_INT_EQ(test_c_library_user_open_save(running.instance, &opened), STASIS_OK);
    ASSERT_TRUE(!opened);
    stop(running);
}

TEST_LIST(TEST_CASE(printf_buffers_stdout_by_the_line), TEST_CASE(stderr_is_written_at_once),
          TEST_CASE(a_failed_assert_writes_to_stderr_and_traps),
          TEST_CASE(time_reads_the_clock_hook_and_is_0_without_one),
          TEST_CASE(arc4random_draws_from_the_entropy_hook_and_traps_without_one),
          TEST_CASE(getenv_finds_nothing), TEST_CASE(exit_flushes_stdout_and_traps_as_exit),
          TEST_CASE(fopen_fails));
