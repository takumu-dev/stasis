/* The system interface: the load check of the functions a script imports from
 * `wasi_snapshot_preview1`, and what each of them does, with the library's hooks and without them,
 * called through system.wasm with the arguments each case chooses. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_system.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The errnos of WASI preview 1 that the system interface returns. */
enum {
    ERRNO_SUCCESS = 0,
    ERRNO_BADF = 8,
    ERRNO_INVAL = 28,
    ERRNO_NOTSUP = 58,
    ERRNO_SPIPE = 70,
};

/* The pointer that the cases' hooks are given as their user data. */
static int hook_user_data;

/* One call of the output hook, as the cases' hook records it. */
struct output_call {
    stasis_instance *instance;
    void *user_data;
    stasis_stream stream;
    char bytes[16];
    size_t size;
};

static struct output_call output_calls[4];
static size_t output_call_count;

/* The message that the output hook aborts the call with, or null for none. */
static const char *output_abort;

static void record_output(stasis_instance *instance, void *user_data, stasis_stream stream,
                          const char *bytes, size_t size) {
    ASSERT_TRUE(output_call_count < sizeof output_calls / sizeof output_calls[0]);
    ASSERT_TRUE(size <= sizeof output_calls[0].bytes);
    struct output_call *call = &output_calls[output_call_count++];
    call->instance = instance;
    call->user_data = user_data;
    call->stream = stream;
    memcpy(call->bytes, bytes, size);
    call->size = size;
    if (output_abort != NULL) {
        ASSERT_INT_EQ(stasis_abort(instance, output_abort), STASIS_OK);
    }
}

/* Fails unless the output hook's call `index` was for `instance`, with the hooks' user data, and
 * gave it the `size` bytes at `bytes` on `stream`. */
static void expect_output(size_t index, const stasis_instance *instance, stasis_stream stream,
                          const char *bytes, size_t size) {
    const struct output_call *call = &output_calls[index];
    ASSERT_TRUE(call->instance == instance);
    ASSERT_TRUE(call->user_data == &hook_user_data);
    ASSERT_INT_EQ(call->stream, stream);
    ASSERT_SIZE_EQ(call->size, size);
    ASSERT_MEM_EQ(call->bytes, bytes, size);
}

/* The calls of the clock hook, as the cases' hook records them: how many there were, and what the
 * last was given. */
static struct {
    size_t count;
    stasis_instance *instance;
    void *user_data;
    uint32_t clock;
    stasis_clock_reading reading;
    uint64_t precision;
} clock_calls;

/* What the clock hook does: whether it answers, the nanoseconds it writes whether it answers or
 * not, and the message it aborts the call with, or null for none. */
static struct {
    bool answers;
    uint64_t nanoseconds;
    const char *abort;
} clock_reply;

static bool read_clock(stasis_instance *instance, void *user_data, uint32_t clock,
                       stasis_clock_reading reading, uint64_t precision, uint64_t *nanoseconds) {
    clock_calls.count++;
    clock_calls.instance = instance;
    clock_calls.user_data = user_data;
    clock_calls.clock = clock;
    clock_calls.reading = reading;
    clock_calls.precision = precision;
    *nanoseconds = clock_reply.nanoseconds;
    if (clock_reply.abort != NULL) {
        ASSERT_INT_EQ(stasis_abort(instance, clock_reply.abort), STASIS_OK);
    }
    return clock_reply.answers;
}

/* Fails unless the clock hook was called once since the case set it, for `instance`, with the
 * hooks' user data, to read `reading` of `clock` with `precision`. */
static void expect_clock_call(const stasis_instance *instance, uint32_t clock,
                              stasis_clock_reading reading, uint64_t precision) {
    ASSERT_SIZE_EQ(clock_calls.count, 1);
    ASSERT_TRUE(clock_calls.instance == instance);
    ASSERT_TRUE(clock_calls.user_data == &hook_user_data);
    ASSERT_UINT_EQ(clock_calls.clock, clock);
    ASSERT_INT_EQ(clock_calls.reading, reading);
    ASSERT_UINT_EQ(clock_calls.precision, precision);
    clock_calls.count = 0;
}

/* The calls of the entropy hook, as the cases' hook records them: how many there were, and what the
 * last was given. */
static struct {
    size_t count;
    stasis_instance *instance;
    void *user_data;
    size_t size;
} entropy_calls;

/* What the entropy hook does: whether it answers, and the message it aborts the call with, or null
 * for none. */
static struct {
    bool answers;
    const char *abort;
} entropy_reply;

/* Fills the buffer with the bytes 1, 2, 3 and so on, whether it answers or not. */
static bool draw_entropy(stasis_instance *instance, void *user_data, void *buffer, size_t size) {
    entropy_calls.count++;
    entropy_calls.instance = instance;
    entropy_calls.user_data = user_data;
    entropy_calls.size = size;
    for (size_t i = 0; i < size; i++) {
        ((unsigned char *)buffer)[i] = (unsigned char)(i + 1);
    }
    if (entropy_reply.abort != NULL) {
        ASSERT_INT_EQ(stasis_abort(instance, entropy_reply.abort), STASIS_OK);
    }
    return entropy_reply.answers;
}

/* Fails unless the entropy hook was called once since the case set it, for `instance`, with the
 * hooks' user data, to fill `size` bytes. */
static void expect_entropy_call(const stasis_instance *instance, size_t size) {
    ASSERT_SIZE_EQ(entropy_calls.count, 1);
    ASSERT_TRUE(entropy_calls.instance == instance);
    ASSERT_TRUE(entropy_calls.user_data == &hook_user_data);
    ASSERT_SIZE_EQ(entropy_calls.size, size);
    entropy_calls.count = 0;
}

/* Forgets every call of the hooks, and sets them to answer, and not to abort. */
static void reset_hooks(void) {
    output_call_count = 0;
    output_abort = NULL;
    memset(&clock_calls, 0, sizeof clock_calls);
    clock_reply.answers = true;
    clock_reply.nanoseconds = 0;
    clock_reply.abort = NULL;
    memset(&entropy_calls, 0, sizeof entropy_calls);
    entropy_reply.answers = true;
    entropy_reply.abort = NULL;
}

/* Settings with each of the cases' hooks, and the hooks' user data. */
static stasis_library_settings with_hooks(void) {
    stasis_library_settings settings;
    memset(&settings, 0, sizeof settings);
    settings.output = record_output;
    settings.clock = read_clock;
    settings.entropy = draw_entropy;
    settings.hook_user_data = &hook_user_data;
    return settings;
}

/* An instance of system.wasm, started, with its script and its library. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

/* Starts an instance of system.wasm in a library of its own with `settings`, or the defaults when
 * it's null, and resets the hooks. */
static struct running start_system(const stasis_library_settings *settings) {
    reset_hooks();
    struct running running;
    ASSERT_INT_EQ(stasis_library_create(settings, &running.library), STASIS_OK);
    running.script = load_script(running.library, &test_system_interface, "system");
    running.instance = start_instance(running.script);
    return running;
}

static void stop(struct running running) {
    destroy_all(running.instance, running.script, running.library);
}

/* The errno that the function of the system interface returned, in the last call that
 * EXPECT_ERRNO made. */
static uint32_t returned_errno;

/* Fails, as the assertion `assertion` on `line`, unless a call that EXPECT_ERRNO made returned
 * `status` STASIS_OK, and its function the errno `expected`. */
static void expect_errno(int line, const char *assertion, stasis_status status, uint32_t expected) {
    harness_assert_int(__FILE__, line, assertion, status, STASIS_OK);
    harness_assert_uint(__FILE__, line, assertion, returned_errno, expected);
}

/* Calls `function`, an entry point of system.wasm, with the arguments that follow, which must
 * return, and its function of the system interface the errno `expected`. */
#define EXPECT_ERRNO(expected, function, ...)                                                      \
    expect_errno(__LINE__, "EXPECT_ERRNO(" #expected ", " #function ")",                           \
                 test_system_user_##function(__VA_ARGS__, &returned_errno), (expected))

/* The indices of the functions system.wasm imports, which it imports in order of name. */
enum {
    ARGS_SIZES_GET_IMPORT = 1,
    CLOCK_RES_GET_IMPORT = 2,
    CLOCK_TIME_GET_IMPORT = 3,
    ENVIRON_SIZES_GET_IMPORT = 5,
    FD_FDSTAT_GET_IMPORT = 10,
    FD_READ_IMPORT = 20,
    FD_WRITE_IMPORT = 26,
    PROC_EXIT_IMPORT = 38,
    RANDOM_GET_IMPORT = 40,
};

/* Fails unless `instance` has trapped as `kind` with `message`, in its call of the function that
 * system.wasm imports at `import`, from the entry point that called it. */
static void expect_trap(const stasis_instance *instance, stasis_trap_kind kind, const char *message,
                        uint32_t import) {
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, kind);
    ASSERT_STR_EQ(trap->message, message);
    ASSERT_SIZE_EQ(trap->frame_count, 2);
    ASSERT_UINT_EQ(trap->frames[0].function_index, import);
    ASSERT_STR_EQ(trap->frames[0].function_name, NULL);
    ASSERT_UINT_EQ(trap->frames[0].module_offset, STASIS_NO_OFFSET);
    ASSERT_TRUE(trap->frames[1].function_index > import);
}

/* Where the cases of calls that fail point every pointer they pass, at bytes that they set first,
 * so that they can see that a call that fails writes nothing. */
enum {
    UNTOUCHED_AT = 1024,
    UNTOUCHED_SIZE = 64,
};

static const uint64_t untouched = 0xA5A5A5A5A5A5A5A5U;

static void set_untouched(stasis_instance *instance) {
    for (uint32_t at = UNTOUCHED_AT; at < UNTOUCHED_AT + UNTOUCHED_SIZE; at += 8) {
        ASSERT_INT_EQ(test_system_user_store_u64(instance, at, untouched), STASIS_OK);
    }
}

static void expect_untouched(stasis_instance *instance) {
    for (uint32_t at = UNTOUCHED_AT; at < UNTOUCHED_AT + UNTOUCHED_SIZE; at += 8) {
        uint64_t value = 0;
        ASSERT_INT_EQ(test_system_user_load_u64(instance, at, &value), STASIS_OK);
        ASSERT_UINT_EQ(value, untouched);
    }
}

/* Stores the `size` bytes at `bytes` at `address` in the memory of `instance`. */
static void store_bytes(stasis_instance *instance, uint32_t address, const char *bytes,
                        size_t size) {
    for (size_t i = 0; i < size; i++) {
        ASSERT_INT_EQ(test_system_user_store_u8(instance, address + (uint32_t)i, (uint8_t)bytes[i]),
                      STASIS_OK);
    }
}

/* Stores an iovec at `address` in the memory of `instance`, of the `size` bytes at `bytes`. */
static void store_iovec(stasis_instance *instance, uint32_t address, uint32_t bytes,
                        uint32_t size) {
    ASSERT_INT_EQ(test_system_user_store_u32(instance, address, bytes), STASIS_OK);
    ASSERT_INT_EQ(test_system_user_store_u32(instance, address + 4, size), STASIS_OK);
}

/* Where the cases of output put the bytes that their iovecs hold. */
enum { BYTES_AT = 2048 };

/* The size of system.wasm's memory, where its end is. */
static const uint32_t memory_end = 16 * 65536;

/* Fails unless a call into the instance of `running`, which returned `status`, trapped as a
 * boundary violation with `message`, in its call of the function that system.wasm imports at
 * `import`, before any hook was called. Then destroys the instance, its script and its library. */
static void expect_violation(struct running running, stasis_status status, const char *message,
                             uint32_t import) {
    ASSERT_INT_EQ(status, STASIS_TRAP);
    expect_trap(running.instance, STASIS_TRAP_KIND_BOUNDARY_VIOLATION, message, import);
    ASSERT_SIZE_EQ(output_call_count, 0);
    ASSERT_SIZE_EQ(clock_calls.count, 0);
    ASSERT_SIZE_EQ(entropy_calls.count, 0);
    stop(running);
}

/* Runs `body` in an instance of system.wasm in a library without hooks, and then in one with every
 * hook, none of which it may call. */
static void with_and_without_hooks(void (*body)(stasis_instance *instance)) {
    const stasis_library_settings hooked = with_hooks();
    const stasis_library_settings *const settings[] = {NULL, &hooked};
    for (size_t i = 0; i < sizeof settings / sizeof settings[0]; i++) {
        const struct running running = start_system(settings[i]);
        body(running.instance);
        ASSERT_SIZE_EQ(output_call_count, 0);
        ASSERT_SIZE_EQ(clock_calls.count, 0);
        ASSERT_SIZE_EQ(entropy_calls.count, 0);
        stop(running);
    }
}

/* Loads `name` against system.stasis, which must fail with `status` and the message `expected`. */
static void expect_refused(const char *name, stasis_status status, const char *expected) {
    stasis_library *library = create_library();
    const struct module_bytes module = read_module(name);
    char message[2048] = "not written";
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, &test_system_interface, module.bytes, module.size,
                                     message, sizeof message, &script),
                  status);
    ASSERT_STR_EQ(message, expected);
    ASSERT_TRUE(script == NULL);
    free_module(module);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* system.wasm imports all 46 functions, each of its own type. */
TEST(loads_a_script_that_imports_every_function) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_system_interface, "system");
    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* An import that isn't a function breaks only the rule against such imports. */
TEST(refuses_functions_of_other_types_and_functions_it_lacks) {
    expect_refused("system_wrong_types", STASIS_INVALID_SCRIPT,
                   "the script imports the global `clock` from `wasi_snapshot_preview1`, where a "
                   "script may import only functions\n"
                   "the script imports `fd_write` from `wasi_snapshot_preview1` as a function of "
                   "type (i32, i32, i32) -> (i32), where the system interface's is (i32, i32, "
                   "i32, i32) -> (i32)\n"
                   "the script imports `proc_exit` from `wasi_snapshot_preview1` as a function of "
                   "type (i32) -> (i32), where the system interface's is (i32) -> ()\n"
                   "the script imports `fd_frobnicate` from `wasi_snapshot_preview1`, which the "
                   "system interface lacks");
}

/* Loads the 64 bits at `address` in the memory of `instance`. */
static uint64_t load_u64(stasis_instance *instance, uint32_t address) {
    uint64_t value = 0;
    ASSERT_INT_EQ(test_system_user_load_u64(instance, address, &value), STASIS_OK);
    return value;
}

/* The bytes set first, after a call has stored a u32 of 0 in their low half. */
static const uint64_t zero_stored = 0xA5A5A5A500000000U;

/* A script has no arguments and no environment: the calls that size them store 0 in each u32 they
 * answer, and the calls that get them write nothing. */
static void size_and_get_the_arguments(stasis_instance *instance) {
    set_untouched(instance);
    const uint32_t at = UNTOUCHED_AT;
    EXPECT_ERRNO(ERRNO_SUCCESS, args_sizes_get, instance, at, at + 8);
    ASSERT_UINT_EQ(load_u64(instance, at), zero_stored);
    ASSERT_UINT_EQ(load_u64(instance, at + 8), zero_stored);
    EXPECT_ERRNO(ERRNO_SUCCESS, environ_sizes_get, instance, at + 16, at + 24);
    ASSERT_UINT_EQ(load_u64(instance, at + 16), zero_stored);
    ASSERT_UINT_EQ(load_u64(instance, at + 24), zero_stored);
    EXPECT_ERRNO(ERRNO_SUCCESS, args_get, instance, at + 32, at + 40);
    EXPECT_ERRNO(ERRNO_SUCCESS, environ_get, instance, at + 48, at + 56);
    for (uint32_t offset = 32; offset < UNTOUCHED_SIZE; offset += 8) {
        ASSERT_UINT_EQ(load_u64(instance, at + offset), untouched);
    }
}

TEST(arguments_and_the_environment_are_empty) {
    with_and_without_hooks(size_and_get_the_arguments);
}

/* What fd_fdstat_get answers: a character device's file type, and the rights to read and to
 * write. */
enum {
    FILETYPE_CHARACTER_DEVICE = 2,
    RIGHTS_FD_READ = 1 << 1,
    RIGHTS_FD_WRITE = 1 << 6,
};

/* Fds 0 to 2 are character devices without the rights to seek or tell, which is what wasi-libc's
 * isatty looks for, and so what makes it buffer stdout by the line. Fd 0 has only the right to
 * read, and fds 1 and 2 only the right to write. Each fdstat is 24 bytes, all written, its padding
 * and flags as 0. */
static void stat_fds_0_to_2(stasis_instance *instance) {
    const uint32_t at = UNTOUCHED_AT;
    for (uint32_t fd = 0; fd <= 2; fd++) {
        set_untouched(instance);
        EXPECT_ERRNO(ERRNO_SUCCESS, fd_fdstat_get, instance, fd, at);
        ASSERT_UINT_EQ(load_u64(instance, at), FILETYPE_CHARACTER_DEVICE);
        ASSERT_UINT_EQ(load_u64(instance, at + 8), fd == 0 ? RIGHTS_FD_READ : RIGHTS_FD_WRITE);
        ASSERT_UINT_EQ(load_u64(instance, at + 16), 0);
        ASSERT_UINT_EQ(load_u64(instance, at + 24), untouched);
    }
}

TEST(fds_0_to_2_are_character_devices) { with_and_without_hooks(stat_fds_0_to_2); }

/* Fd 0 always reads as the end of the file: reading it stores 0 in the u32 it answers, the number
 * of bytes read, and leaves the buffer alone. */
static void read_fd_0(stasis_instance *instance) {
    set_untouched(instance);
    const uint32_t at = UNTOUCHED_AT;
    ASSERT_INT_EQ(test_system_user_store_u32(instance, at, at + 16), STASIS_OK);
    ASSERT_INT_EQ(test_system_user_store_u32(instance, at + 4, 8), STASIS_OK);
    EXPECT_ERRNO(ERRNO_SUCCESS, fd_read, instance, 0, at, 1, at + 8);
    ASSERT_UINT_EQ(load_u64(instance, at + 8), zero_stored);
    ASSERT_UINT_EQ(load_u64(instance, at + 16), untouched);
}

TEST(fd_0_reads_as_the_end_of_the_file) { with_and_without_hooks(read_fd_0); }

/* A library keeps its hooks for its life: it copies its settings, so changing them once it's
 * created changes nothing. Each hook is given the instance it acts for, whichever of the library's
 * instances that is. */
TEST(a_library_keeps_its_hooks_and_gives_each_the_instance_it_acts_for) {
    stasis_library_settings settings = with_hooks();
    const struct running running = start_system(&settings);
    memset(&settings, 0, sizeof settings);
    stasis_instance *other = start_instance(running.script);
    store_bytes(other, BYTES_AT, "other", 5);
    store_iovec(other, UNTOUCHED_AT, BYTES_AT, 5);
    store_bytes(running.instance, BYTES_AT, "first", 5);
    store_iovec(running.instance, UNTOUCHED_AT, BYTES_AT, 5);
    EXPECT_ERRNO(ERRNO_SUCCESS, fd_write, other, 1, UNTOUCHED_AT, 1, UNTOUCHED_AT + 8);
    EXPECT_ERRNO(ERRNO_SUCCESS, fd_write, running.instance, 1, UNTOUCHED_AT, 1, UNTOUCHED_AT + 8);
    ASSERT_SIZE_EQ(output_call_count, 2);
    expect_output(0, other, STASIS_STREAM_STDOUT, "other", 5);
    expect_output(1, running.instance, STASIS_STREAM_STDOUT, "first", 5);
    EXPECT_ERRNO(ERRNO_SUCCESS, clock_time_get, other, 1, 1, UNTOUCHED_AT + 16);
    expect_clock_call(other, 1, STASIS_CLOCK_TIME, 1);
    EXPECT_ERRNO(ERRNO_SUCCESS, random_get, other, UNTOUCHED_AT + 24, 4);
    expect_entropy_call(other, 4);
    ASSERT_INT_EQ(stasis_instance_destroy(other), STASIS_OK);
    stop(running);
}

/* fd_write gives the output hook each of its iovecs that holds any bytes, in order, as they are:
 * Stasis doesn't buffer them, split them into lines or check them as UTF-8. It stores the number of
 * bytes written in the u32 it answers. */
TEST(fd_write_gives_the_output_hook_the_bytes_as_written) {
    const stasis_library_settings settings = with_hooks();
    const struct running running = start_system(&settings);
    stasis_instance *instance = running.instance;
    set_untouched(instance);
    const uint32_t at = UNTOUCHED_AT;
    store_bytes(instance, BYTES_AT, "hello, world\n\xff", 14);
    store_iovec(instance, at, BYTES_AT, 7);
    store_iovec(instance, at + 8, BYTES_AT, 0);
    store_iovec(instance, at + 16, BYTES_AT + 7, 7);
    EXPECT_ERRNO(ERRNO_SUCCESS, fd_write, instance, 1, at, 3, at + 24);
    ASSERT_UINT_EQ(load_u64(instance, at + 24), zero_stored | 14);
    ASSERT_SIZE_EQ(output_call_count, 2);
    expect_output(0, instance, STASIS_STREAM_STDOUT, "hello, ", 7);
    expect_output(1, instance, STASIS_STREAM_STDOUT, "world\n\xff", 7);
    EXPECT_ERRNO(ERRNO_SUCCESS, fd_write, instance, 2, at + 16, 1, at + 24);
    ASSERT_UINT_EQ(load_u64(instance, at + 24), zero_stored | 7);
    ASSERT_SIZE_EQ(output_call_count, 3);
    expect_output(2, instance, STASIS_STREAM_STDERR, "world\n\xff", 7);
    stop(running);
}

/* Without an output hook, fd_write discards the bytes, and still succeeds. */
TEST(fd_write_without_an_output_hook_discards_the_bytes) {
    const struct running running = start_system(NULL);
    stasis_instance *instance = running.instance;
    set_untouched(instance);
    const uint32_t at = UNTOUCHED_AT;
    store_iovec(instance, at, BYTES_AT, 7);
    store_iovec(instance, at + 8, BYTES_AT, 5);
    for (uint32_t fd = 1; fd <= 2; fd++) {
        EXPECT_ERRNO(ERRNO_SUCCESS, fd_write, instance, fd, at, 2, at + 16);
        ASSERT_UINT_EQ(load_u64(instance, at + 16), zero_stored | 12);
    }
    stop(running);
}

/* The number of bytes written is a u32, so a write of iovecs whose sizes add up to more fails with
 * inval, before the output hook is given any of them. These are 65,537 iovecs of the 65,536 bytes
 * at the start of memory. */
TEST(fd_write_fails_when_its_sizes_add_up_to_more_than_a_u32) {
    const stasis_library_settings settings = with_hooks();
    const struct running running = start_system(&settings);
    stasis_instance *instance = running.instance;
    set_untouched(instance);
    const uint32_t count = 65537;
    const uint32_t iovecs = 65536;
    ASSERT_INT_EQ(test_system_user_store_iovecs(instance, iovecs, count, 0, 65536), STASIS_OK);
    EXPECT_ERRNO(ERRNO_INVAL, fd_write, instance, 1, iovecs, count, UNTOUCHED_AT);
    ASSERT_SIZE_EQ(output_call_count, 0);
    expect_untouched(instance);
    EXPECT_ERRNO(ERRNO_SUCCESS, fd_write, instance, 1, iovecs, 0, UNTOUCHED_AT);
    ASSERT_UINT_EQ(load_u64(instance, UNTOUCHED_AT), zero_stored);
    ASSERT_SIZE_EQ(output_call_count, 0);
    stop(running);
}

/* The output hook may abort the call, which ends as soon as the hook returns, so the hook is given
 * none of the iovecs after. */
TEST(the_output_hook_may_abort_the_call) {
    const stasis_library_settings settings = with_hooks();
    const struct running running = start_system(&settings);
    stasis_instance *instance = running.instance;
    const uint32_t at = UNTOUCHED_AT;
    store_iovec(instance, at, BYTES_AT, 7);
    store_iovec(instance, at + 8, BYTES_AT, 7);
    output_abort = "too much output";
    uint32_t errno_value = 0;
    ASSERT_INT_EQ(test_system_user_fd_write(instance, 1, at, 2, at + 16, &errno_value),
                  STASIS_TRAP);
    ASSERT_SIZE_EQ(output_call_count, 1);
    expect_trap(instance, STASIS_TRAP_KIND_HOST_ABORT, "too much output", FD_WRITE_IMPORT);
    stop(running);
}

/* A script that passes fd_write iovecs outside its memory or misaligned, or bytes outside its
 * memory, or a return area outside its memory or misaligned, traps before any of its bytes reach
 * the output hook, even when its sizes add up to more than a u32. */
TEST(fd_write_traps_on_what_isnt_in_memory) {
    const stasis_library_settings settings = with_hooks();
    uint32_t errno_value = 0;
    struct running running = start_system(&settings);
    expect_violation(
        running, test_system_user_fd_write(running.instance, 1, memory_end - 8, 2, 0, &errno_value),
        "the script passed `wasi_snapshot_preview1.fd_write` an iovec array at "
        "1048568 in its memory, where its 16 bytes run past its end at 1048576",
        FD_WRITE_IMPORT);
    running = start_system(&settings);
    expect_violation(running,
                     test_system_user_fd_write(running.instance, 1, 1026, 1, 0, &errno_value),
                     "the script passed `wasi_snapshot_preview1.fd_write` an iovec array at 1026 "
                     "in its memory, which isn't a multiple of its alignment, 4",
                     FD_WRITE_IMPORT);
    running = start_system(&settings);
    store_iovec(running.instance, UNTOUCHED_AT, BYTES_AT, 7);
    store_iovec(running.instance, UNTOUCHED_AT + 8, memory_end - 4, 8);
    expect_violation(running,
                     test_system_user_fd_write(running.instance, 1, UNTOUCHED_AT, 2,
                                               UNTOUCHED_AT + 16, &errno_value),
                     "the script passed `wasi_snapshot_preview1.fd_write` the bytes of iovec 1 at "
                     "1048572 in its memory, where its 8 bytes run past its end at 1048576",
                     FD_WRITE_IMPORT);
    running = start_system(&settings);
    store_iovec(running.instance, UNTOUCHED_AT, BYTES_AT, 7);
    expect_violation(
        running,
        test_system_user_fd_write(running.instance, 2, UNTOUCHED_AT, 1, memory_end, &errno_value),
        "the script passed `wasi_snapshot_preview1.fd_write` a return area at "
        "1048576 in its memory, where its 4 bytes run past its end at 1048576",
        FD_WRITE_IMPORT);
    running = start_system(&settings);
    store_iovec(running.instance, UNTOUCHED_AT, BYTES_AT, 7);
    expect_violation(
        running,
        test_system_user_fd_write(running.instance, 2, UNTOUCHED_AT, 1, 1030, &errno_value),
        "the script passed `wasi_snapshot_preview1.fd_write` a return area at 1030 "
        "in its memory, which isn't a multiple of its alignment, 4",
        FD_WRITE_IMPORT);
    running = start_system(&settings);
    ASSERT_INT_EQ(test_system_user_store_iovecs(running.instance, 65536, 65537, 0, 65536),
                  STASIS_OK);
    expect_violation(
        running,
        test_system_user_fd_write(running.instance, 1, 65536, 65537, memory_end, &errno_value),
        "the script passed `wasi_snapshot_preview1.fd_write` a return area at "
        "1048576 in its memory, where its 4 bytes run past its end at 1048576",
        FD_WRITE_IMPORT);
}

/* Each of the other calls that writes into memory traps when where it writes is outside the memory
 * or misaligned, before it calls a hook. */
TEST(writes_outside_memory_or_misaligned_trap) {
    const stasis_library_settings settings = with_hooks();
    uint32_t errno_value = 0;
    struct running running = start_system(&settings);
    expect_violation(running,
                     test_system_user_random_get(running.instance, memory_end - 4, 5, &errno_value),
                     "the script passed `wasi_snapshot_preview1.random_get` a buffer at 1048572 "
                     "in its memory, where its 5 bytes run past its end at 1048576",
                     RANDOM_GET_IMPORT);
    running = start_system(&settings);
    expect_violation(running,
                     test_system_user_clock_time_get(running.instance, 0, 1, 1028, &errno_value),
                     "the script passed `wasi_snapshot_preview1.clock_time_get` a return area at "
                     "1028 in its memory, which isn't a multiple of its alignment, 8",
                     CLOCK_TIME_GET_IMPORT);
    running = start_system(&settings);
    expect_violation(
        running, test_system_user_clock_res_get(running.instance, 1, memory_end - 4, &errno_value),
        "the script passed `wasi_snapshot_preview1.clock_res_get` a return area at "
        "1048572 in its memory, which isn't a multiple of its alignment, 8",
        CLOCK_RES_GET_IMPORT);
    running = start_system(&settings);
    expect_violation(running,
                     test_system_user_clock_res_get(running.instance, 1, memory_end, &errno_value),
                     "the script passed `wasi_snapshot_preview1.clock_res_get` a return area at "
                     "1048576 in its memory, where its 8 bytes run past its end at 1048576",
                     CLOCK_RES_GET_IMPORT);
    running = start_system(NULL);
    expect_violation(running,
                     test_system_user_fd_read(running.instance, 0, UNTOUCHED_AT, 1, memory_end - 2,
                                              &errno_value),
                     "the script passed `wasi_snapshot_preview1.fd_read` a return area at 1048574 "
                     "in its memory, which isn't a multiple of its alignment, 4",
                     FD_READ_IMPORT);
    running = start_system(NULL);
    expect_violation(
        running, test_system_user_fd_fdstat_get(running.instance, 1, memory_end - 16, &errno_value),
        "the script passed `wasi_snapshot_preview1.fd_fdstat_get` a return area at "
        "1048560 in its memory, where its 24 bytes run past its end at 1048576",
        FD_FDSTAT_GET_IMPORT);
    running = start_system(NULL);
    expect_violation(running,
                     test_system_user_fd_fdstat_get(running.instance, 2, 1028, &errno_value),
                     "the script passed `wasi_snapshot_preview1.fd_fdstat_get` a return area at "
                     "1028 in its memory, which isn't a multiple of its alignment, 8",
                     FD_FDSTAT_GET_IMPORT);
    running = start_system(NULL);
    expect_violation(
        running,
        test_system_user_args_sizes_get(running.instance, UNTOUCHED_AT, memory_end, &errno_value),
        "the script passed `wasi_snapshot_preview1.args_sizes_get` a return area at "
        "1048576 in its memory, where its 4 bytes run past its end at 1048576",
        ARGS_SIZES_GET_IMPORT);
    running = start_system(NULL);
    expect_violation(
        running,
        test_system_user_environ_sizes_get(running.instance, 1025, UNTOUCHED_AT, &errno_value),
        "the script passed `wasi_snapshot_preview1.environ_sizes_get` a return area "
        "at 1025 in its memory, which isn't a multiple of its alignment, 4",
        ENVIRON_SIZES_GET_IMPORT);
}

/* clock_time_get gives the clock hook the clock and the precision as the script passes them, any
 * clock included, and stores the time the hook answers as a u64. */
TEST(clock_time_get_reads_the_clock_hook) {
    const stasis_library_settings settings = with_hooks();
    const struct running running = start_system(&settings);
    stasis_instance *instance = running.instance;
    set_untouched(instance);
    const uint32_t at = UNTOUCHED_AT;
    clock_reply.nanoseconds = 0x0123456789ABCDEFU;
    EXPECT_ERRNO(ERRNO_SUCCESS, clock_time_get, instance, 1, 1000, at);
    expect_clock_call(instance, 1, STASIS_CLOCK_TIME, 1000);
    ASSERT_UINT_EQ(load_u64(instance, at), 0x0123456789ABCDEFU);
    clock_reply.nanoseconds = 7;
    EXPECT_ERRNO(ERRNO_SUCCESS, clock_time_get, instance, 99, UINT64_MAX, at + 8);
    expect_clock_call(instance, 99, STASIS_CLOCK_TIME, UINT64_MAX);
    ASSERT_UINT_EQ(load_u64(instance, at + 8), 7);
    ASSERT_UINT_EQ(load_u64(instance, at + 16), untouched);
    stop(running);
}

/* clock_res_get asks the clock hook for the clock's resolution, with a precision of 0. */
TEST(clock_res_get_reads_the_clock_hook) {
    const stasis_library_settings settings = with_hooks();
    const struct running running = start_system(&settings);
    stasis_instance *instance = running.instance;
    set_untouched(instance);
    clock_reply.nanoseconds = 1000000;
    EXPECT_ERRNO(ERRNO_SUCCESS, clock_res_get, instance, 0, UNTOUCHED_AT);
    expect_clock_call(instance, 0, STASIS_CLOCK_RESOLUTION, 0);
    ASSERT_UINT_EQ(load_u64(instance, UNTOUCHED_AT), 1000000);
    ASSERT_UINT_EQ(load_u64(instance, UNTOUCHED_AT + 8), untouched);
    stop(running);
}

/* Without a clock hook, reading any clock fails with notsup, and writes nothing. */
TEST(without_a_clock_hook_reading_a_clock_fails) {
    const struct running running = start_system(NULL);
    stasis_instance *instance = running.instance;
    set_untouched(instance);
    for (uint32_t clock = 0; clock <= 3; clock++) {
        EXPECT_ERRNO(ERRNO_NOTSUP, clock_time_get, instance, clock, 1, UNTOUCHED_AT);
        EXPECT_ERRNO(ERRNO_NOTSUP, clock_res_get, instance, clock, UNTOUCHED_AT + 8);
    }
    expect_untouched(instance);
    stop(running);
}

/* When the clock hook declines, the read fails with notsup, and what the hook wrote isn't
 * stored. */
TEST(a_clock_hook_that_declines_fails_the_read) {
    const stasis_library_settings settings = with_hooks();
    const struct running running = start_system(&settings);
    stasis_instance *instance = running.instance;
    set_untouched(instance);
    clock_reply.answers = false;
    clock_reply.nanoseconds = 42;
    EXPECT_ERRNO(ERRNO_NOTSUP, clock_time_get, instance, 0, 1, UNTOUCHED_AT);
    expect_clock_call(instance, 0, STASIS_CLOCK_TIME, 1);
    EXPECT_ERRNO(ERRNO_NOTSUP, clock_res_get, instance, 1, UNTOUCHED_AT + 8);
    expect_clock_call(instance, 1, STASIS_CLOCK_RESOLUTION, 0);
    expect_untouched(instance);
    stop(running);
}

/* The clock hook may abort the call, whichever reading it's asked for. */
TEST(the_clock_hook_may_abort_the_call) {
    const stasis_library_settings settings = with_hooks();
    uint32_t errno_value = 0;
    struct running running = start_system(&settings);
    clock_reply.abort = "the clock stopped";
    ASSERT_INT_EQ(
        test_system_user_clock_time_get(running.instance, 0, 1, UNTOUCHED_AT, &errno_value),
        STASIS_TRAP);
    expect_trap(running.instance, STASIS_TRAP_KIND_HOST_ABORT, "the clock stopped",
                CLOCK_TIME_GET_IMPORT);
    stop(running);
    running = start_system(&settings);
    clock_reply.abort = "the clock stopped";
    ASSERT_INT_EQ(test_system_user_clock_res_get(running.instance, 0, UNTOUCHED_AT, &errno_value),
                  STASIS_TRAP);
    expect_trap(running.instance, STASIS_TRAP_KIND_HOST_ABORT, "the clock stopped",
                CLOCK_RES_GET_IMPORT);
    stop(running);
}

/* random_get has the entropy hook fill its buffer, with as many bytes as it asks for, none
 * included. */
TEST(random_get_fills_its_buffer_from_the_entropy_hook) {
    const stasis_library_settings settings = with_hooks();
    const struct running running = start_system(&settings);
    stasis_instance *instance = running.instance;
    set_untouched(instance);
    const uint32_t at = UNTOUCHED_AT;
    EXPECT_ERRNO(ERRNO_SUCCESS, random_get, instance, at, 12);
    expect_entropy_call(instance, 12);
    ASSERT_UINT_EQ(load_u64(instance, at), 0x0807060504030201U);
    ASSERT_UINT_EQ(load_u64(instance, at + 8), 0xA5A5A5A50C0B0A09U);
    EXPECT_ERRNO(ERRNO_SUCCESS, random_get, instance, at + 16, 0);
    expect_entropy_call(instance, 0);
    ASSERT_UINT_EQ(load_u64(instance, at + 16), untouched);
    stop(running);
}

/* Without an entropy hook, or when it declines, random_get fails with notsup, and writes nothing,
 * whatever the hook wrote. */
TEST(random_get_fails_without_bytes_from_an_entropy_hook) {
    struct running running = start_system(NULL);
    set_untouched(running.instance);
    EXPECT_ERRNO(ERRNO_NOTSUP, random_get, running.instance, UNTOUCHED_AT, 16);
    expect_untouched(running.instance);
    stop(running);
    const stasis_library_settings settings = with_hooks();
    running = start_system(&settings);
    set_untouched(running.instance);
    entropy_reply.answers = false;
    EXPECT_ERRNO(ERRNO_NOTSUP, random_get, running.instance, UNTOUCHED_AT, 16);
    expect_entropy_call(running.instance, 16);
    expect_untouched(running.instance);
    stop(running);
}

/* The entropy hook may abort the call. */
TEST(the_entropy_hook_may_abort_the_call) {
    const stasis_library_settings settings = with_hooks();
    const struct running running = start_system(&settings);
    entropy_reply.abort = "no randomness here";
    uint32_t errno_value = 0;
    ASSERT_INT_EQ(test_system_user_random_get(running.instance, UNTOUCHED_AT, 8, &errno_value),
                  STASIS_TRAP);
    expect_trap(running.instance, STASIS_TRAP_KIND_HOST_ABORT, "no randomness here",
                RANDOM_GET_IMPORT);
    stop(running);
}

/* While Stasis is inside the script's cabi_realloc or a cabi_post_, the script may call no import,
 * the system interface's included: one that does traps as a boundary violation, before the
 * function runs, so even proc_exit doesn't exit. */
TEST(no_function_can_be_called_from_cabi_realloc_or_cabi_post) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_system_interface, "system_may_leave");
    stasis_instance *instance = start_instance(script);
    const stasis_string text = {"hi", 2};
    ASSERT_INT_EQ(test_system_user_take(instance, text), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_BOUNDARY_VIOLATION);
    ASSERT_STR_EQ(trap->message, "the script called `wasi_snapshot_preview1.sched_yield` from its "
                                 "`cabi_realloc`, where it may call no import");
    ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_OK);
    instance = start_instance(script);
    stasis_string given = {"not given", 9};
    ASSERT_INT_EQ(test_system_user_give(instance, &given), STASIS_TRAP);
    trap = stasis_instance_trap(instance);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_BOUNDARY_VIOLATION);
    ASSERT_STR_EQ(trap->message, "the script called `wasi_snapshot_preview1.proc_exit` from its "
                                 "`cabi_post_*` for `user.give`, where it may call no import");
    destroy_all(instance, script, library);
}

/* proc_exit ends the call as a trap, whatever the status, 0 included, which ends the instance, with
 * the hooks or without them. A status is unsigned. */
TEST(proc_exit_traps_as_exit_with_its_status) {
    const uint32_t statuses[] = {0, 1, UINT32_MAX};
    const char *const messages[] = {
        "the script exited with status 0",
        "the script exited with status 1",
        "the script exited with status 4294967295",
    };
    const stasis_library_settings hooked = with_hooks();
    for (size_t i = 0; i < sizeof statuses / sizeof statuses[0]; i++) {
        const struct running running = start_system(i % 2 == 0 ? NULL : &hooked);
        ASSERT_INT_EQ(test_system_user_proc_exit(running.instance, statuses[i]), STASIS_TRAP);
        expect_trap(running.instance, STASIS_TRAP_KIND_EXIT, messages[i], PROC_EXIT_IMPORT);
        ASSERT_SIZE_EQ(output_call_count, 0);
        uint32_t errno_value = 0;
        ASSERT_INT_EQ(test_system_user_sched_yield(running.instance, &errno_value),
                      STASIS_REFUSED_TRAPPED);
        stop(running);
    }
}

static void call_sched_yield(stasis_instance *instance) {
    EXPECT_ERRNO(ERRNO_SUCCESS, sched_yield, instance);
}

TEST(sched_yield_returns_at_once) { with_and_without_hooks(call_sched_yield); }

/* A call on an fd above 2 fails with badf before it reads or writes anything, whichever of its
 * arguments the fd is. An fd is unsigned, so -1 is above 2 too. The iovecs and paths the calls
 * pass are the bytes set first, which name nothing in the script's memory. */
static void call_on_fds_above_2(stasis_instance *instance) {
    set_untouched(instance);
    const uint32_t at = UNTOUCHED_AT;
    const uint32_t fds[] = {3, 4, UINT32_MAX};
    for (size_t i = 0; i < sizeof fds / sizeof fds[0]; i++) {
        const uint32_t fd = fds[i];
        EXPECT_ERRNO(ERRNO_BADF, fd_advise, instance, fd, 0, 8, 0);
        EXPECT_ERRNO(ERRNO_BADF, fd_allocate, instance, fd, 0, 8);
        EXPECT_ERRNO(ERRNO_BADF, fd_close, instance, fd);
        EXPECT_ERRNO(ERRNO_BADF, fd_datasync, instance, fd);
        EXPECT_ERRNO(ERRNO_BADF, fd_fdstat_get, instance, fd, at);
        EXPECT_ERRNO(ERRNO_BADF, fd_fdstat_set_flags, instance, fd, 0);
        EXPECT_ERRNO(ERRNO_BADF, fd_fdstat_set_rights, instance, fd, 0, 0);
        EXPECT_ERRNO(ERRNO_BADF, fd_filestat_get, instance, fd, at);
        EXPECT_ERRNO(ERRNO_BADF, fd_filestat_set_size, instance, fd, 0);
        EXPECT_ERRNO(ERRNO_BADF, fd_filestat_set_times, instance, fd, 0, 0, 0);
        EXPECT_ERRNO(ERRNO_BADF, fd_pread, instance, fd, at, 1, 0, at);
        EXPECT_ERRNO(ERRNO_BADF, fd_prestat_dir_name, instance, fd, at, 8);
        EXPECT_ERRNO(ERRNO_BADF, fd_prestat_get, instance, fd, at);
        EXPECT_ERRNO(ERRNO_BADF, fd_pwrite, instance, fd, at, 1, 0, at);
        EXPECT_ERRNO(ERRNO_BADF, fd_read, instance, fd, at, 1, at);
        EXPECT_ERRNO(ERRNO_BADF, fd_readdir, instance, fd, at, 8, 0, at);
        EXPECT_ERRNO(ERRNO_BADF, fd_renumber, instance, fd, 1);
        EXPECT_ERRNO(ERRNO_BADF, fd_renumber, instance, 1, fd);
        EXPECT_ERRNO(ERRNO_BADF, fd_seek, instance, fd, 0, 0, at);
        EXPECT_ERRNO(ERRNO_BADF, fd_sync, instance, fd);
        EXPECT_ERRNO(ERRNO_BADF, fd_tell, instance, fd, at);
        EXPECT_ERRNO(ERRNO_BADF, fd_write, instance, fd, at, 1, at);
        EXPECT_ERRNO(ERRNO_BADF, path_create_directory, instance, fd, at, 8);
        EXPECT_ERRNO(ERRNO_BADF, path_filestat_get, instance, fd, 0, at, 8, at);
        EXPECT_ERRNO(ERRNO_BADF, path_filestat_set_times, instance, fd, 0, at, 8, 0, 0, 0);
        EXPECT_ERRNO(ERRNO_BADF, path_link, instance, fd, 0, at, 8, 1, at, 8);
        EXPECT_ERRNO(ERRNO_BADF, path_link, instance, 1, 0, at, 8, fd, at, 8);
        EXPECT_ERRNO(ERRNO_BADF, path_open, instance, fd, 0, at, 8, 0, 0, 0, 0, at);
        EXPECT_ERRNO(ERRNO_BADF, path_readlink, instance, fd, at, 8, at, 8, at);
        EXPECT_ERRNO(ERRNO_BADF, path_remove_directory, instance, fd, at, 8);
        EXPECT_ERRNO(ERRNO_BADF, path_rename, instance, fd, at, 8, 1, at, 8);
        EXPECT_ERRNO(ERRNO_BADF, path_rename, instance, 1, at, 8, fd, at, 8);
        EXPECT_ERRNO(ERRNO_BADF, path_symlink, instance, at, 8, fd, at, 8);
        EXPECT_ERRNO(ERRNO_BADF, path_unlink_file, instance, fd, at, 8);
        EXPECT_ERRNO(ERRNO_BADF, sock_accept, instance, fd, 0, at);
        EXPECT_ERRNO(ERRNO_BADF, sock_recv, instance, fd, at, 1, 0, at, at + 8);
        EXPECT_ERRNO(ERRNO_BADF, sock_send, instance, fd, at, 1, 0, at);
        EXPECT_ERRNO(ERRNO_BADF, sock_shutdown, instance, fd, 0);
    }
    expect_untouched(instance);
}

TEST(every_call_on_an_fd_above_2_fails_with_badf) { with_and_without_hooks(call_on_fds_above_2); }

/* Fds 0 to 2 are streams, which can't be sought or told. */
static void seek_and_tell_fds_0_to_2(stasis_instance *instance) {
    set_untouched(instance);
    for (uint32_t fd = 0; fd <= 2; fd++) {
        EXPECT_ERRNO(ERRNO_SPIPE, fd_seek, instance, fd, 0, 0, UNTOUCHED_AT);
        EXPECT_ERRNO(ERRNO_SPIPE, fd_tell, instance, fd, UNTOUCHED_AT);
    }
    expect_untouched(instance);
}

TEST(seeking_or_telling_on_fds_0_to_2_fails_with_spipe) {
    with_and_without_hooks(seek_and_tell_fds_0_to_2);
}

/* Every call that the other cases don't show doing something fails with notsup, and writes nothing:
 * there are no files, preopened directories or sockets, fd 0 can only be read and fds 1 and 2
 * only written, and a script never blocks, so it can't poll. */
static void call_what_only_fails(stasis_instance *instance) {
    set_untouched(instance);
    const uint32_t at = UNTOUCHED_AT;
    for (uint32_t fd = 0; fd <= 2; fd++) {
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_advise, instance, fd, 0, 8, 0);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_allocate, instance, fd, 0, 8);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_close, instance, fd);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_datasync, instance, fd);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_fdstat_set_flags, instance, fd, 0);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_fdstat_set_rights, instance, fd, 0, 0);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_filestat_get, instance, fd, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_filestat_set_size, instance, fd, 0);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_filestat_set_times, instance, fd, 0, 0, 0);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_pread, instance, fd, at, 1, 0, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_prestat_dir_name, instance, fd, at, 8);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_prestat_get, instance, fd, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_pwrite, instance, fd, at, 1, 0, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_readdir, instance, fd, at, 8, 0, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_renumber, instance, fd, 2 - fd);
        EXPECT_ERRNO(ERRNO_NOTSUP, fd_sync, instance, fd);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_create_directory, instance, fd, at, 8);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_filestat_get, instance, fd, 0, at, 8, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_filestat_set_times, instance, fd, 0, at, 8, 0, 0, 0);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_link, instance, fd, 0, at, 8, 2 - fd, at, 8);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_open, instance, fd, 0, at, 8, 0, 0, 0, 0, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_readlink, instance, fd, at, 8, at, 8, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_remove_directory, instance, fd, at, 8);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_rename, instance, fd, at, 8, 2 - fd, at, 8);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_symlink, instance, at, 8, fd, at, 8);
        EXPECT_ERRNO(ERRNO_NOTSUP, path_unlink_file, instance, fd, at, 8);
        EXPECT_ERRNO(ERRNO_NOTSUP, sock_accept, instance, fd, 0, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, sock_recv, instance, fd, at, 1, 0, at, at + 8);
        EXPECT_ERRNO(ERRNO_NOTSUP, sock_send, instance, fd, at, 1, 0, at);
        EXPECT_ERRNO(ERRNO_NOTSUP, sock_shutdown, instance, fd, 0);
    }
    EXPECT_ERRNO(ERRNO_NOTSUP, fd_write, instance, 0, at, 1, at);
    EXPECT_ERRNO(ERRNO_NOTSUP, fd_read, instance, 1, at, 1, at);
    EXPECT_ERRNO(ERRNO_NOTSUP, fd_read, instance, 2, at, 1, at);
    EXPECT_ERRNO(ERRNO_NOTSUP, poll_oneoff, instance, at, at, 1, at);
    EXPECT_ERRNO(ERRNO_NOTSUP, proc_raise, instance, 6);
    expect_untouched(instance);
}

TEST(every_other_call_fails_with_notsup) { with_and_without_hooks(call_what_only_fails); }

TEST_LIST(TEST_CASE(loads_a_script_that_imports_every_function),
          TEST_CASE(refuses_functions_of_other_types_and_functions_it_lacks),
          TEST_CASE(fds_0_to_2_are_character_devices), TEST_CASE(fd_0_reads_as_the_end_of_the_file),
          TEST_CASE(a_library_keeps_its_hooks_and_gives_each_the_instance_it_acts_for),
          TEST_CASE(fd_write_gives_the_output_hook_the_bytes_as_written),
          TEST_CASE(fd_write_without_an_output_hook_discards_the_bytes),
          TEST_CASE(fd_write_fails_when_its_sizes_add_up_to_more_than_a_u32),
          TEST_CASE(the_output_hook_may_abort_the_call),
          TEST_CASE(fd_write_traps_on_what_isnt_in_memory),
          TEST_CASE(writes_outside_memory_or_misaligned_trap),
          TEST_CASE(clock_time_get_reads_the_clock_hook),
          TEST_CASE(clock_res_get_reads_the_clock_hook),
          TEST_CASE(without_a_clock_hook_reading_a_clock_fails),
          TEST_CASE(a_clock_hook_that_declines_fails_the_read),
          TEST_CASE(the_clock_hook_may_abort_the_call),
          TEST_CASE(random_get_fills_its_buffer_from_the_entropy_hook),
          TEST_CASE(random_get_fails_without_bytes_from_an_entropy_hook),
          TEST_CASE(the_entropy_hook_may_abort_the_call),
          TEST_CASE(no_function_can_be_called_from_cabi_realloc_or_cabi_post),
          TEST_CASE(arguments_and_the_environment_are_empty),
          TEST_CASE(proc_exit_traps_as_exit_with_its_status),
          TEST_CASE(sched_yield_returns_at_once),
          TEST_CASE(every_call_on_an_fd_above_2_fails_with_badf),
          TEST_CASE(seeking_or_telling_on_fds_0_to_2_fails_with_spipe),
          TEST_CASE(every_other_call_fails_with_notsup));
