#include "system_interface.h"

#include <stasis.h>
#include <stasis_interface.h>

#include "allocator.h"
#include "boundary.h"
#include "calls.h"
#include "descriptors.h"
#include "module.h"
#include "names.h"
/* NOLINTNEXTLINE(misc-include-cleaner): it defines the struct that stasis.h's type names */
#include "objects.h"
#include "trap.h"
#include "wamr_hooks.h"

#include <lib_export.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wasm_export.h>

/* The errnos of WASI preview 1 that the system interface returns. */
enum {
    ERRNO_SUCCESS = 0,
    ERRNO_BADF = 8,
    ERRNO_FAULT = 21,
    ERRNO_INVAL = 28,
    ERRNO_NOTSUP = 58,
    ERRNO_SPIPE = 70,
};

/* The fds that a script may name: its standard input, output and error. */
enum { LAST_FD = 2 };

/* The size of an iovec, which holds the address of its bytes and then their size, each a u32, and
 * its alignment. */
enum {
    IOVEC_SIZE = 8,
    IOVEC_ALIGNMENT = 4,
};

/* The room for how a message names an iovec's bytes, such as "the bytes of iovec 4294967295". */
enum { IOVEC_NAME_SIZE = 32 };

/* What fd_fdstat_get answers of fds 0 to 2, as WASI preview 1 numbers it. */
enum {
    FILETYPE_CHARACTER_DEVICE = 2,
    RIGHTS_FD_READ = 1 << 1,
    RIGHTS_FD_WRITE = 1 << 6,
};

/* The size of an fdstat, its alignment, and where it holds its rights, after its file type and its
 * flags. */
enum {
    FDSTAT_SIZE = 24,
    FDSTAT_ALIGNMENT = 8,
    FDSTAT_RIGHTS_BASE = 8,
};

struct system_function;

/* How a function of the system interface answers the running `instance`, which called it with its
 * core arguments, each in a slot of its own. It returns the errno the script gets, or raises a trap
 * of Stasis's own, which ends the call, and then what it returns is ignored. */
typedef uint32_t system_answer(stasis_instance *instance, const struct system_function *function,
                               const uint64_t *core);

/* One of the system interface's functions. */
struct system_function {
    const char *name;
    /* Its core type, as WAMR writes a host function's: its parameters in brackets, then its
     * result, each `i` for an i32 and `I` for an i64, and `d` for an i32 that's an fd. */
    const char *signature;
    /* How it answers when none of its fds is above 2, or null if it only fails, with notsup. */
    system_answer *answer;
};

/* Sets `at` to where the `size` bytes at `address` in the running instance's memory are, which the
 * script passed `function` as `what`, if they're all in the memory and `address` is a multiple of
 * `alignment`. Returns false otherwise, having raised a boundary violation. */
static bool reach_memory(stasis_instance *instance, const struct system_function *function,
                         const char *what, uint32_t address, uint64_t size, uint32_t alignment,
                         unsigned char **at) {
    const struct script_memory memory = stasis_script_memory(instance->module_instance);
    if (address % alignment != 0) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed `" STASIS_SYSTEM_INTERFACE_MODULE
                                ".%s` %s at %lu in its memory, which isn't a multiple of its "
                                "alignment, %lu",
                                function->name, what, (unsigned long)address,
                                (unsigned long)alignment);
        return false;
    }
    if ((uint64_t)address + size > memory.size) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_BOUNDARY_VIOLATION,
                                "the script passed `" STASIS_SYSTEM_INTERFACE_MODULE
                                ".%s` %s at %lu in its memory, where its %llu bytes run past its "
                                "end at %llu",
                                function->name, what, (unsigned long)address,
                                (unsigned long long)size, (unsigned long long)memory.size);
        return false;
    }
    *at = memory.base + address;
    return true;
}

/* The settings of the library of `instance`, which hold the hooks. */
static const stasis_library_settings *library_settings(const stasis_instance *instance) {
    return &instance->script->library->settings;
}

/* Stores `value` little-endian in the 8 bytes at `at`, in a script's memory. */
static void store_u64(unsigned char *at, uint64_t value) {
    stasis_store_u32(at, (uint32_t)value);
    stasis_store_u32(at + 4, (uint32_t)(value >> 32));
}

/* Succeeds at once: a script has no arguments and no environment to get, and nothing to yield
 * to. */
static uint32_t system_succeed(stasis_instance *instance, const struct system_function *function,
                               const uint64_t *core) {
    (void)instance;
    (void)function;
    (void)core;
    return ERRNO_SUCCESS;
}

/* Stores 0 in the two u32s that sizing the arguments or the environment answers: the number of
 * them, and the bytes they need. */
static uint32_t system_size_nothing(stasis_instance *instance,
                                    const struct system_function *function, const uint64_t *core) {
    unsigned char *count = NULL;
    unsigned char *size = NULL;
    if (!reach_memory(instance, function, "a return area", (uint32_t)core[0], 4, 4, &count) ||
        !reach_memory(instance, function, "a return area", (uint32_t)core[1], 4, 4, &size)) {
        return ERRNO_FAULT;
    }
    stasis_store_u32(count, 0);
    stasis_store_u32(size, 0);
    return ERRNO_SUCCESS;
}

/* Fds 0 to 2 are character devices without the rights to seek or tell, which is what wasi-libc's
 * isatty looks for, and so what makes it buffer stdout by the line. Fd 0 may only be read, and fds
 * 1 and 2 only written. Every byte of the fdstat is written, its padding and flags as 0, so that
 * the same state gives the same memory. */
static uint32_t system_fd_fdstat_get(stasis_instance *instance,
                                     const struct system_function *function, const uint64_t *core) {
    unsigned char *fdstat = NULL;
    if (!reach_memory(instance, function, "a return area", (uint32_t)core[1], FDSTAT_SIZE,
                      FDSTAT_ALIGNMENT, &fdstat)) {
        return ERRNO_FAULT;
    }
    memset(fdstat, 0, FDSTAT_SIZE);
    fdstat[0] = FILETYPE_CHARACTER_DEVICE;
    store_u64(fdstat + FDSTAT_RIGHTS_BASE,
              (uint32_t)core[0] == 0 ? RIGHTS_FD_READ : RIGHTS_FD_WRITE);
    return ERRNO_SUCCESS;
}

/* Writes the bytes of each iovec that holds any to the output hook, if there's one, as fd 1 or 2's
 * stream, and discards them if there's none. Every iovec is checked first, so that the hook is
 * given none of the bytes of a write that fails or traps. Fd 0 can't be written. */
static uint32_t system_fd_write(stasis_instance *instance, const struct system_function *function,
                                const uint64_t *core) {
    const uint32_t fd = (uint32_t)core[0];
    if (fd == 0) {
        return ERRNO_NOTSUP;
    }
    const uint32_t count = (uint32_t)core[2];
    unsigned char *iovecs = NULL;
    if (!reach_memory(instance, function, "an iovec array", (uint32_t)core[1],
                      (uint64_t)count * IOVEC_SIZE, IOVEC_ALIGNMENT, &iovecs)) {
        return ERRNO_FAULT;
    }
    const struct script_memory memory = stasis_script_memory(instance->module_instance);
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        const unsigned char *iovec = iovecs + ((size_t)i * IOVEC_SIZE);
        const uint32_t address = stasis_load_u32(iovec);
        const uint32_t size = stasis_load_u32(iovec + 4);
        if ((uint64_t)address + size > memory.size) {
            char what[IOVEC_NAME_SIZE];
            (void)snprintf(what, sizeof what, "the bytes of iovec %lu", (unsigned long)i);
            unsigned char *bytes = NULL;
            (void)reach_memory(instance, function, what, address, size, 1, &bytes);
            return ERRNO_FAULT;
        }
        total += size;
    }
    unsigned char *written = NULL;
    if (!reach_memory(instance, function, "a return area", (uint32_t)core[3], 4, 4, &written)) {
        return ERRNO_FAULT;
    }
    /* The number of bytes written is a u32. */
    if (total > UINT32_MAX) {
        return ERRNO_INVAL;
    }
    const stasis_library_settings *settings = library_settings(instance);
    const stasis_stream stream = fd == 1 ? STASIS_STREAM_STDOUT : STASIS_STREAM_STDERR;
    for (uint32_t i = 0; i < count && settings->output != NULL; i++) {
        const unsigned char *iovec = iovecs + ((size_t)i * IOVEC_SIZE);
        const uint32_t address = stasis_load_u32(iovec);
        const uint32_t size = stasis_load_u32(iovec + 4);
        if (size == 0) {
            continue;
        }
        settings->output(instance, settings->hook_user_data, stream,
                         (const char *)memory.base + address, size);
        /* A hook that aborted the call ends it at once. */
        if (instance->raised.raised) {
            return ERRNO_SUCCESS;
        }
    }
    stasis_store_u32(written, (uint32_t)total);
    return ERRNO_SUCCESS;
}

/* Fd 0 always reads as the end of the file, and fds 1 and 2 can't be read. */
static uint32_t system_fd_read(stasis_instance *instance, const struct system_function *function,
                               const uint64_t *core) {
    if ((uint32_t)core[0] != 0) {
        return ERRNO_NOTSUP;
    }
    unsigned char *read = NULL;
    if (!reach_memory(instance, function, "a return area", (uint32_t)core[3], 4, 4, &read)) {
        return ERRNO_FAULT;
    }
    stasis_store_u32(read, 0);
    return ERRNO_SUCCESS;
}

/* Reads `reading` of `clock` from the clock hook, with `precision`, into the u64 at `address` in
 * the script's memory. Without a hook, or when the hook declines, the read fails and writes
 * nothing. */
static uint32_t read_clock(stasis_instance *instance, const struct system_function *function,
                           uint32_t clock, stasis_clock_reading reading, uint64_t precision,
                           uint32_t address) {
    unsigned char *at = NULL;
    if (!reach_memory(instance, function, "a return area", address, 8, 8, &at)) {
        return ERRNO_FAULT;
    }
    const stasis_library_settings *settings = library_settings(instance);
    if (settings->clock == NULL) {
        return ERRNO_NOTSUP;
    }
    uint64_t nanoseconds = 0;
    const bool answered = settings->clock(instance, settings->hook_user_data, clock, reading,
                                          precision, &nanoseconds);
    /* A hook that aborted the call ends it, whatever it answered. */
    if (instance->raised.raised || !answered) {
        return ERRNO_NOTSUP;
    }
    store_u64(at, nanoseconds);
    return ERRNO_SUCCESS;
}

/* Reads a clock's resolution from the clock hook. */
static uint32_t system_clock_res_get(stasis_instance *instance,
                                     const struct system_function *function, const uint64_t *core) {
    return read_clock(instance, function, (uint32_t)core[0], STASIS_CLOCK_RESOLUTION, 0,
                      (uint32_t)core[1]);
}

/* Reads a clock's time from the clock hook, with the precision the script asks for. */
static uint32_t system_clock_time_get(stasis_instance *instance,
                                      const struct system_function *function,
                                      const uint64_t *core) {
    return read_clock(instance, function, (uint32_t)core[0], STASIS_CLOCK_TIME, core[1],
                      (uint32_t)core[2]);
}

/* Has the entropy hook fill a buffer of Stasis's own, which is copied into the script's memory
 * once the hook has answered, so that a hook that declines changes nothing, whatever it wrote.
 * Without a hook, or when the hook declines, it fails. */
static uint32_t system_random_get(stasis_instance *instance, const struct system_function *function,
                                  const uint64_t *core) {
    const uint32_t size = (uint32_t)core[1];
    unsigned char *at = NULL;
    if (!reach_memory(instance, function, "a buffer", (uint32_t)core[0], size, 1, &at)) {
        return ERRNO_FAULT;
    }
    const stasis_library_settings *settings = library_settings(instance);
    if (settings->entropy == NULL) {
        return ERRNO_NOTSUP;
    }
    /* At least a byte, so that the hook is given a buffer even when it's asked for none. */
    unsigned char *bytes = stasis_allocate(size > 0 ? size : 1);
    if (bytes == NULL) {
        (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_ALLOCATION_FAILED,
                                "there was no memory for the %lu random bytes the script asked "
                                "`" STASIS_SYSTEM_INTERFACE_MODULE ".random_get` for",
                                (unsigned long)size);
        return ERRNO_FAULT;
    }
    const bool answered = settings->entropy(instance, settings->hook_user_data, bytes, size);
    /* A hook that aborted the call ends it, whatever it answered. */
    if (answered && !instance->raised.raised && size > 0) {
        memcpy(at, bytes, size);
    }
    stasis_deallocate(bytes);
    return answered ? ERRNO_SUCCESS : ERRNO_NOTSUP;
}

/* Ends the call as a trap, whatever the status: by then, a C script's `exit` has run its `atexit`
 * handlers and flushed its streams, so the instance is over. */
static uint32_t system_proc_exit(stasis_instance *instance, const struct system_function *function,
                                 const uint64_t *core) {
    (void)function;
    (void)stasis_raise_trap(&instance->raised, STASIS_TRAP_KIND_EXIT,
                            "the script exited with status %lu", (unsigned long)(uint32_t)core[0]);
    return ERRNO_SUCCESS;
}

/* Fds 0 to 2 are streams, which can't be sought or told. */
static uint32_t system_refuse_seek(stasis_instance *instance,
                                   const struct system_function *function, const uint64_t *core) {
    (void)instance;
    (void)function;
    (void)core;
    return ERRNO_SPIPE;
}

/* The functions, by name, each of the type that WASI preview 1 gives it. WAMR takes each as a
 * pointer it may write through, its attachment, though it never does. */
static struct system_function functions[] = {
    {"args_get", "(ii)i", system_succeed},
    {"args_sizes_get", "(ii)i", system_size_nothing},
    {"clock_res_get", "(ii)i", system_clock_res_get},
    {"clock_time_get", "(iIi)i", system_clock_time_get},
    {"environ_get", "(ii)i", system_succeed},
    {"environ_sizes_get", "(ii)i", system_size_nothing},
    {"fd_advise", "(dIIi)i", NULL},
    {"fd_allocate", "(dII)i", NULL},
    {"fd_close", "(d)i", NULL},
    {"fd_datasync", "(d)i", NULL},
    {"fd_fdstat_get", "(di)i", system_fd_fdstat_get},
    {"fd_fdstat_set_flags", "(di)i", NULL},
    {"fd_fdstat_set_rights", "(dII)i", NULL},
    {"fd_filestat_get", "(di)i", NULL},
    {"fd_filestat_set_size", "(dI)i", NULL},
    {"fd_filestat_set_times", "(dIIi)i", NULL},
    {"fd_pread", "(diiIi)i", NULL},
    {"fd_prestat_dir_name", "(dii)i", NULL},
    {"fd_prestat_get", "(di)i", NULL},
    {"fd_pwrite", "(diiIi)i", NULL},
    {"fd_read", "(diii)i", system_fd_read},
    {"fd_readdir", "(diiIi)i", NULL},
    {"fd_renumber", "(dd)i", NULL},
    {"fd_seek", "(dIii)i", system_refuse_seek},
    {"fd_sync", "(d)i", NULL},
    {"fd_tell", "(di)i", system_refuse_seek},
    {"fd_write", "(diii)i", system_fd_write},
    {"path_create_directory", "(dii)i", NULL},
    {"path_filestat_get", "(diiii)i", NULL},
    {"path_filestat_set_times", "(diiiIIi)i", NULL},
    {"path_link", "(diiidii)i", NULL},
    {"path_open", "(diiiiIIii)i", NULL},
    {"path_readlink", "(diiiii)i", NULL},
    {"path_remove_directory", "(dii)i", NULL},
    {"path_rename", "(diidii)i", NULL},
    {"path_symlink", "(iidii)i", NULL},
    {"path_unlink_file", "(dii)i", NULL},
    {"poll_oneoff", "(iiii)i", NULL},
    {"proc_exit", "(i)", system_proc_exit},
    {"proc_raise", "(i)i", NULL},
    {"random_get", "(ii)i", system_random_get},
    {"sched_yield", "()i", system_succeed},
    {"sock_accept", "(dii)i", NULL},
    {"sock_recv", "(diiiii)i", NULL},
    {"sock_send", "(diiii)i", NULL},
    {"sock_shutdown", "(di)i", NULL},
};

enum { FUNCTION_COUNT = sizeof functions / sizeof functions[0] };

_Static_assert(FUNCTION_COUNT == 46, "The system interface has every function of WASI preview 1.");

/* The value type that a character of a signature stands for. */
static uint8_t value_type(char character) { return character == 'I' ? MODULE_I64 : MODULE_I32; }

bool stasis_system_function_type(stasis_string name, struct core_type *type) {
    for (size_t i = 0; i < FUNCTION_COUNT; i++) {
        if (!stasis_string_is(name, functions[i].name)) {
            continue;
        }
        const char *at = functions[i].signature + 1;
        type->parameter_count = 0;
        for (; *at != ')'; at++) {
            type->parameters[type->parameter_count++] = value_type(*at);
        }
        type->result_count = 0;
        for (at++; *at != '\0'; at++) {
            type->results[type->result_count++] = value_type(*at);
        }
        return true;
    }
    return false;
}

/* Whether any fd among the core arguments of a call to `function` is above 2, which a script never
 * has open. */
static bool names_closed_fd(const struct system_function *function, const uint64_t *core) {
    for (size_t n = 0; function->signature[n + 1] != ')'; n++) {
        if (function->signature[n + 1] == 'd' && (uint32_t)core[n] > LAST_FD) {
            return true;
        }
    }
    return false;
}

/* What WAMR calls in place of each of the system interface's functions, in its raw calling
 * convention: with the script's core arguments, each in a slot of its own, and the slot that the
 * result goes in first. The function is its attachment, and the instance is its execution
 * environment's user data. */
static void trampoline(wasm_exec_env_t exec_env, uint64_t *core) {
    const struct system_function *function = wasm_runtime_get_function_attachment(exec_env);
    stasis_instance *instance = wasm_runtime_get_user_data(exec_env);
    if (!stasis_may_leave(instance)) {
        /* The message names the function as it names a host function, with the module in place
         * of the group. */
        const struct host_function_name name = {STASIS_SYSTEM_INTERFACE_MODULE, "", "",
                                                function->name};
        stasis_refuse_leaving(instance, &name);
        stasis_end_call(instance);
        return;
    }
    uint32_t errno_value = ERRNO_NOTSUP;
    if (names_closed_fd(function, core)) {
        errno_value = ERRNO_BADF;
    } else if (function->answer != NULL) {
        errno_value = function->answer(instance, function, core);
    }
    /* proc_exit, the one function without a result, always ends the call. */
    if (instance->raised.raised) {
        stasis_end_call(instance);
        return;
    }
    const stasis_value u32 = {STASIS_TYPE_U32, 0, 0};
    stasis_lower_raw_scalar(&u32, &errno_value, core);
}

/* The functions as WAMR links them, a symbol for each, which WAMR sorts in place and keeps for as
 * long as it's initialised. */
static NativeSymbol symbols[FUNCTION_COUNT];

bool stasis_system_interface_register(void) {
    for (size_t i = 0; i < FUNCTION_COUNT; i++) {
        symbols[i].symbol = functions[i].name;
        symbols[i].func_ptr = stasis_wamr_raw_pointer(trampoline);
        symbols[i].signature = NULL;
        symbols[i].attachment = &functions[i];
    }
    return wasm_runtime_register_natives_raw(STASIS_SYSTEM_INTERFACE_MODULE, symbols,
                                             FUNCTION_COUNT);
}
