/**
 * @file stasis.h
 * @brief Stasis's C API.
 *
 * Stasis is a game scripting library. It runs scripts compiled to WebAssembly in a sandbox inside a
 * host, and saves and restores their state as snapshots.
 *
 * Every function that can fail returns a ::stasis_status, and hands its results back through
 * out-parameters.
 *
 * This header is C99 that also compiles as C++11 or later, and it may include nothing beyond
 * `<stddef.h>`, `<stdint.h>` and `<stdbool.h>`.
 */

#ifndef STASIS_H
#define STASIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def STASIS_API
 * @brief Marks a function of Stasis's API.
 *
 * Stasis exports only the functions this marks. A host that links Stasis as a DLL on Windows
 * defines `STASIS_SHARED`, as Stasis's CMake target does for it, which makes this import them.
 */
#ifdef _WIN32
#ifdef STASIS_EXPORTS
#define STASIS_API __declspec(dllexport)
#elif defined(STASIS_SHARED)
#define STASIS_API __declspec(dllimport)
#else
#define STASIS_API
#endif
#elif defined(__GNUC__)
#define STASIS_API __attribute__((visibility("default")))
#else
#define STASIS_API
#endif

/**
 * @brief How a call ended.
 *
 * Every function that can fail returns one of these: ::STASIS_OK when it succeeded, and otherwise a
 * code for each outcome a host might handle differently. A code's number never changes and is
 * never reused, but any minor release may add codes, so a host handles codes it doesn't know.
 * The type is 32 bits wide, and holds any code a later release adds.
 * stasis_status_text() describes each code.
 */
typedef enum stasis_status {
    /** The call succeeded. */
    STASIS_OK = 0,
    /**
     * Starting a script instance, or calling one of its entry points, trapped. The instance has
     * trapped for good, and its trap report says how.
     */
    STASIS_TRAP = 1,
    /**
     * Refused, because a script instance, this one or another, is already running on this thread.
     * Calls into scripts never nest.
     */
    STASIS_REFUSED_NESTED = 2,
    /** Refused, because the script instance is running, so it can't be snapshotted or destroyed. */
    STASIS_REFUSED_RUNNING = 3,
    /**
     * Refused, because the script instance has trapped. A trapped instance can only have its trap
     * report read, or be destroyed.
     */
    STASIS_REFUSED_TRAPPED = 4,
    /**
     * Refused, because the script instance was created but hasn't started, so none of its entry
     * points can be called and it can't be snapshotted.
     */
    STASIS_REFUSED_NOT_STARTED = 5,
    /** Refused, because the script instance has already started, or was restored. */
    STASIS_REFUSED_STARTED = 6,
    /** Refused, because the script doesn't provide that optional entry point. */
    STASIS_REFUSED_ABSENT = 7,
    /**
     * Refused, because the script isn't snapshottable, so its instances can't be snapshotted or
     * restored.
     */
    STASIS_REFUSED_NOT_SNAPSHOTTABLE = 8,
    /**
     * Refused, because something still needs the object: a script that has instances, or a library
     * that has scripts, can't be destroyed, and the allocator can't be set while any library
     * exists.
     */
    STASIS_REFUSED_IN_USE = 9,
    /** Refused, because the script instance an abort names isn't running on this thread. */
    STASIS_REFUSED_NOT_RUNNING = 10,
    /** Loading failed, because the bytes aren't a valid WebAssembly module. */
    STASIS_INVALID_MODULE = 11,
    /** Loading failed, because the script was built for another interface. */
    STASIS_WRONG_INTERFACE = 12,
    /**
     * Loading failed, because the script was built for another major version of the interface, or
     * under 0.x, another minor version.
     */
    STASIS_INCOMPATIBLE_VERSION = 13,
    /**
     * Loading failed, because the script was built for a compatible version of the interface, but
     * lacks a required entry point, or uses a host function or entry point that the host's
     * interface lacks or declares differently.
     */
    STASIS_INTERFACE_MISMATCH = 14,
    /**
     * Loading failed, because the module is valid WebAssembly but breaks Stasis's rules for
     * scripts.
     */
    STASIS_INVALID_SCRIPT = 15,
    /** Restoring failed, because the snapshot is of another script. */
    STASIS_WRONG_SCRIPT = 16,
    /**
     * Restoring failed, because the snapshot's format is newer than this release of Stasis
     * knows.
     */
    STASIS_SNAPSHOT_TOO_NEW = 17,
    /** Restoring failed, because the snapshot is malformed. */
    STASIS_INVALID_SNAPSHOT = 18,
    /**
     * Restoring failed, because the host objects given for the snapshot's live handles leave one
     * out, give one twice, or give one an object of another kind.
     */
    STASIS_INVALID_BINDINGS = 19,
    /** The allocator returned null. */
    STASIS_OUT_OF_MEMORY = 20,
    /** An argument failed a check, such as a null where an object is needed. */
    STASIS_INVALID_ARGUMENT = 21,
    /** A buffer is smaller than the size Stasis reported for it. */
    STASIS_BUFFER_TOO_SMALL = 22,
    /**
     * Not a code. It makes the type 32 bits wide and able to hold any code a later release adds,
     * both in C++ and with a compiler that makes each enum as small as its values allow.
     */
    STASIS_STATUS_MAX_ENUM = 0x7FFFFFFF,
} stasis_status;

/**
 * @brief Describes a status code in English.
 *
 * @param status Any value, a code this release doesn't know included.
 * @return A fixed description of the code, or "unknown status" for a value that isn't one. It's
 * never null.
 */
STASIS_API const char *stasis_status_text(stasis_status status);

/**
 * @brief A library: the host's settings, and the scripts loaded into it.
 *
 * A process may hold several libraries, and each is independent of the others. It's opaque, and a
 * host only ever holds a pointer to one.
 */
typedef struct stasis_library stasis_library;

/**
 * @brief A script: a WebAssembly module loaded into a library against an interface.
 *
 * It belongs to its library, and its instances belong to it. It's opaque, and a host only ever
 * holds a pointer to one.
 */
typedef struct stasis_script stasis_script;

/**
 * @brief A script instance: one live copy of a script, with its own memory.
 *
 * It's opaque, and a host only ever holds a pointer to one.
 */
typedef struct stasis_instance stasis_instance;

/**
 * @brief A UTF-8 string, passed by value, which isn't NUL-terminated.
 */
typedef struct stasis_string {
    /** The string's bytes. It may be null when the string is empty. */
    const char *data;
    /** The number of bytes in the string. */
    size_t size;
} stasis_string;

/**
 * @brief An interface's descriptors and thunks, which `stasis-gen host` writes as constant data.
 *
 * A host passes the generated data by address, and needs to know nothing of what it holds, which
 * stasis_interface.h declares.
 */
typedef struct stasis_interface stasis_interface;

/**
 * @brief The stream that a script writes output to.
 */
typedef enum stasis_stream {
    /** The script's standard output, its file descriptor 1. */
    STASIS_STREAM_STDOUT = 1,
    /** The script's standard error, its file descriptor 2. */
    STASIS_STREAM_STDERR = 2,
    /** Not a stream. It makes the type 32 bits wide, as ::STASIS_STATUS_MAX_ENUM does. */
    STASIS_STREAM_MAX_ENUM = 0x7FFFFFFF,
} stasis_stream;

/**
 * @brief The output hook, which receives what scripts write to their standard output and error.
 *
 * @param instance The script instance that wrote.
 * @param user_data The library's stasis_library_settings::hook_user_data.
 * @param stream The stream it wrote to.
 * @param bytes The bytes it wrote, as it wrote them, which needn't be UTF-8 or whole lines.
 * @param size The number of bytes.
 */
typedef void stasis_output_hook(stasis_instance *instance, void *user_data, stasis_stream stream,
                                const char *bytes, size_t size);

/**
 * @brief The clock hook, which is scripts' only source of the time.
 *
 * @param instance The script instance that reads the clock.
 * @param user_data The library's stasis_library_settings::hook_user_data.
 * @param clock The WASI clock it reads: 0 for real time, 1 for monotonic time, 2 for the process's
 * CPU time, and 3 for the thread's.
 * @param precision The precision the script asks for, in nanoseconds.
 * @param time Where the hook writes the time, in nanoseconds.
 * @return Whether the hook wrote the time. When it declines, the script's read fails.
 */
typedef bool stasis_clock_hook(stasis_instance *instance, void *user_data, uint32_t clock,
                               uint64_t precision, uint64_t *time);

/**
 * @brief The entropy hook, which is scripts' only source of randomness.
 *
 * @param instance The script instance that asks for random bytes.
 * @param user_data The library's stasis_library_settings::hook_user_data.
 * @param buffer Where the hook writes the random bytes.
 * @param size The number of bytes it writes.
 * @return Whether the hook filled the buffer. When it declines, the script's request fails.
 */
typedef bool stasis_entropy_hook(stasis_instance *instance, void *user_data, void *buffer,
                                 size_t size);

/**
 * @brief The size of the stack that script instances keep their WebAssembly frames and operands
 * on, unless the library's settings give another: 64 KiB.
 */
#define STASIS_DEFAULT_STACK_SIZE 65536

/**
 * @brief A library's settings, which stay fixed for the library's life.
 *
 * Settings that are all zero are the defaults. A hook that's null isn't installed. Scripts can't
 * reach the hooks yet: they will through the system interface, which this release lacks.
 */
typedef struct stasis_library_settings {
    /**
     * The size in bytes of the stack on which each of the library's script instances keeps its
     * WebAssembly frames and operands, beside the native stack of the thread that calls it. It's
     * at most `UINT32_MAX`, and 0 means ::STASIS_DEFAULT_STACK_SIZE.
     */
    size_t stack_size;
    /** The output hook, or null for none, so that output is discarded. */
    stasis_output_hook *output;
    /** The clock hook, or null for none, so that reading any clock fails. */
    stasis_clock_hook *clock;
    /** The entropy hook, or null for none, so that asking for random bytes fails. */
    stasis_entropy_hook *entropy;
    /** The pointer that every hook receives as its `user_data`. */
    void *hook_user_data;
} stasis_library_settings;

/**
 * @brief Creates a library.
 *
 * @param settings The library's settings, which Stasis copies, or null for the defaults.
 * @param library Where the library goes, or null when creating it fails.
 * @return ::STASIS_OK, ::STASIS_OUT_OF_MEMORY, or ::STASIS_INVALID_ARGUMENT when `library` is null
 * or a setting is out of range.
 */
STASIS_API stasis_status stasis_library_create(const stasis_library_settings *settings,
                                               stasis_library **library);

/**
 * @brief Destroys a library, once each of its scripts has been destroyed.
 *
 * @param library The library, or null.
 * @return ::STASIS_OK, which destroying null also returns, or ::STASIS_REFUSED_IN_USE when the
 * library still has scripts, which changes nothing.
 */
STASIS_API stasis_status stasis_library_destroy(stasis_library *library);

/**
 * @brief Loads a WebAssembly module as a script, against an interface.
 *
 * Stasis copies the module's bytes, so the host may free them once this returns. Loading checks
 * the module in stages and stops at the first that fails: it must be valid WebAssembly, built for
 * the interface, and for a version of it compatible with the interface's. A script carries its
 * interface's line, which is the major version, or under 0.x the major and minor versions: one
 * built against `acme:dungeon@1.2.0` says `acme:dungeon@1`, and against `acme:dungeon@0.3.1`,
 * `acme:dungeon@0.3`. After those stages, loading lists every problem it finds, and the code is
 * the first one's.
 *
 * Between compatible versions, older or newer, every entry point that the script provides and the
 * interface declares must have the same signature in both, and the script must provide every
 * required one. It may provide entry points that the interface lacks, which Stasis ignores, as it
 * ignores every export it doesn't recognise. Every host function it imports must be the
 * interface's, with the same signature. So far only scalars cross, so a script may import a host
 * function that takes or returns a string, a record or a handle, but a call to it traps.
 *
 * A script must also keep Stasis's rules, so that creating an instance of it runs none of its code
 * and can fail only for lack of memory. It has no start section, and exports nothing that a
 * backend runs by itself when it creates an instance: `_initialize`, `__wasm_call_ctors` or
 * `__post_instantiate`. It imports only functions, each from one of its interface's groups or from
 * `wasi_snapshot_preview1`, the system interface. Each of its active data and element segments fits
 * the memory or the table it's copied into, as that starts, and its memory starts with at most
 * 65,535 pages. A script that isn't snapshottable still loads, with an empty message, and
 * stasis_script_snapshottable() says why it isn't.
 *
 * @param library The library the script goes into.
 * @param interface_data The interface, as `stasis-gen host` writes it. It must outlive the script.
 * @param bytes The module's bytes. It may be null when `size` is 0.
 * @param size The number of bytes.
 * @param message Where a message goes, or null for none. Stasis writes a NUL-terminated message in
 * English, which names each problem on a line of its own when loading fails, and is empty when it
 * succeeds. A message that doesn't fit is cut short. Its wording may change in any release.
 * @param message_size The size of the buffer `message` points at, which may be 0.
 * @param script Where the script goes, or null when loading fails.
 * @return ::STASIS_OK, or one of:
 * - ::STASIS_INVALID_MODULE: the bytes aren't valid WebAssembly;
 * - ::STASIS_WRONG_INTERFACE: the script was built for another interface;
 * - ::STASIS_INCOMPATIBLE_VERSION: it was built for an incompatible version of the interface;
 * - ::STASIS_INTERFACE_MISMATCH: it lacks a required entry point, provides one with another
 *   signature, or imports a host function that the interface lacks or declares with another
 *   signature;
 * - ::STASIS_INVALID_SCRIPT: it breaks Stasis's rules for scripts, such as with names that don't
 *   parse, entry points of more than one kind of script, an entry point or a host function of
 *   another type than its signature makes it, or any of the rules above;
 * - ::STASIS_OUT_OF_MEMORY;
 * - ::STASIS_INVALID_ARGUMENT: a pointer that's needed is null, a buffer's size doesn't match its
 *   pointer, or the interface's descriptor format is newer than this release reads.
 */
STASIS_API stasis_status stasis_script_load(stasis_library *library,
                                            const stasis_interface *interface_data,
                                            const void *bytes, size_t size, char *message,
                                            size_t message_size, stasis_script **script);

/**
 * @brief Destroys a script, once each of its instances has been destroyed.
 *
 * @param script The script, or null.
 * @return ::STASIS_OK, which destroying null also returns, or ::STASIS_REFUSED_IN_USE when the
 * script still has instances, which changes nothing.
 */
STASIS_API stasis_status stasis_script_destroy(stasis_script *script);

/** @brief The size of a script's identity, in bytes. */
#define STASIS_IDENTITY_SIZE 32

/**
 * @brief A script's identity: the SHA-256 of its module's sections in order, leaving out custom
 * sections.
 *
 * Each section is hashed as its id, its size as the shortest LEB128 that holds it, and its
 * contents. So two copies of a module that differ only in custom sections, such as a copy stripped
 * of its debug information, are the same script, even when the tool that stripped it wrote the
 * sections' sizes in fewer bytes.
 *
 * @param script The script.
 * @return The ::STASIS_IDENTITY_SIZE bytes of the identity, which stay valid until the script is
 * destroyed, or null when `script` is null.
 */
STASIS_API const uint8_t *stasis_script_identity(const stasis_script *script);

/**
 * @brief Whether a script provides an entry point.
 *
 * @param script The script.
 * @param entry_point The entry point's id among those of the script's interface, as for
 * stasis_call().
 * @return true for every required entry point, and for each optional one the script provides.
 * false for any other, and when `script` is null or `entry_point` is out of range.
 */
STASIS_API bool stasis_script_provides(const stasis_script *script, uint32_t entry_point);

/**
 * @brief Whether a script is snapshottable, and if not, why.
 *
 * A script is snapshottable when a snapshot can hold everything its instances change: their one
 * linear memory and their mutable globals. Loading checks that the script defines and exports one
 * memory, which isn't shared, that it contains none of the instructions `table.set`,
 * `table.grow`, `table.fill`, `table.copy`, `table.init`, `elem.drop` and `data.drop`, and that it
 * exports every mutable global it defines, each an `i32`, `i64`, `f32` or `f64`. It also imports
 * only functions, which loading already requires of every script. A script that isn't
 * snapshottable still loads and runs, and its load writes no message, but its instances can't be
 * snapshotted.
 *
 * @param script The script.
 * @param reason Where the reason the script isn't snapshottable goes, or null for none. The reason
 * is in English, names each problem on a line of its own, and stays valid until the script is
 * destroyed. Its wording may change in any release. It's set to null when the script is
 * snapshottable, or when `script` is null.
 * @return Whether the script is snapshottable, which is false when `script` is null.
 */
STASIS_API bool stasis_script_snapshottable(const stasis_script *script, const char **reason);

/**
 * @brief Creates an instance of a script, which has to be started before it's called.
 *
 * Creating an instance runs none of the script's code, so a host function may create one.
 *
 * @param script The script.
 * @param user_data A pointer of the host's, which Stasis never dereferences, and which
 * stasis_instance_user_data() returns.
 * @param instance Where the instance goes, or null when creating it fails.
 * @return ::STASIS_OK, ::STASIS_OUT_OF_MEMORY, or ::STASIS_INVALID_ARGUMENT when `script` or
 * `instance` is null.
 */
STASIS_API stasis_status stasis_instance_create(stasis_script *script, void *user_data,
                                                stasis_instance **instance);

/**
 * @brief Starts a script instance, running its script's initialisation once.
 *
 * Starting runs `stasis_initialize`, if the script exports it, as a call into the instance. Once it
 * has returned, the instance is ready for its entry points to be called.
 *
 * @param instance The script instance, which must have been created and not started.
 * @return ::STASIS_OK once the instance is ready. ::STASIS_TRAP when initialisation trapped, and a
 * refusal when the instance can't start: ::STASIS_REFUSED_NESTED while an instance, this one or
 * another, is running on this thread, as it is in a host function, ::STASIS_REFUSED_TRAPPED, or
 * ::STASIS_REFUSED_STARTED once it has started. ::STASIS_INVALID_ARGUMENT when `instance` is null.
 */
STASIS_API stasis_status stasis_instance_start(stasis_instance *instance);

/**
 * @brief Destroys a script instance.
 *
 * A host function may destroy any instance but the one that called it.
 *
 * @param instance The script instance, or null.
 * @return ::STASIS_OK, which destroying null also returns, or ::STASIS_REFUSED_RUNNING when the
 * instance is running, which changes nothing.
 */
STASIS_API stasis_status stasis_instance_destroy(stasis_instance *instance);

/**
 * @brief The user-data pointer a script instance was created with.
 *
 * @param instance The script instance.
 * @return Its user data, or null when `instance` is null.
 */
STASIS_API void *stasis_instance_user_data(const stasis_instance *instance);

/**
 * @brief Calls one of a script instance's entry points.
 *
 * The typed callers that `stasis-gen host` writes each call it for one entry point, with the
 * arguments and result in their own types. Calls into scripts never nest: while one is running on
 * a thread, as it is in a host function, no instance can be called on that thread.
 *
 * @param instance The script instance to call, which must be ready.
 * @param interface_data The interface the instance's script was loaded against.
 * @param entry_point The entry point's id, which is its index among the entry points of its kind
 * of script. The host's side of an interface names each id with a constant, such as
 * `ACME_DUNGEON_NPC_TICK_ENTRY`.
 * @param arguments The entry point's arguments, in a struct laid out as its descriptor says, or
 * null when it takes none.
 * @param result Where the entry point's result goes, laid out as its descriptor says, or null when
 * it returns nothing. It's zeroed when the call fails. The strings in a result, in records too,
 * stay valid until the next call into the instance, or until the instance is destroyed.
 * @return ::STASIS_OK once the entry point has returned. ::STASIS_TRAP when it trapped, and a
 * refusal when the instance couldn't take the call: ::STASIS_REFUSED_NESTED while an instance, this
 * one or another, is running on this thread, ::STASIS_REFUSED_TRAPPED,
 * ::STASIS_REFUSED_NOT_STARTED, or ::STASIS_REFUSED_ABSENT for an optional entry point the script
 * doesn't provide. ::STASIS_INVALID_ARGUMENT when an argument fails
 * a check: when `instance` is null, `interface_data` isn't the interface its script was loaded
 * against, `entry_point` is out of range, or `arguments` or `result` is null where it's needed. So
 * far only scalars cross, so a call to an entry point that takes or returns a string, a record or
 * a handle is ::STASIS_INVALID_ARGUMENT too.
 */
STASIS_API stasis_status stasis_call(stasis_instance *instance,
                                     const stasis_interface *interface_data, uint32_t entry_point,
                                     const void *arguments, void *result);

/**
 * @brief What made a script instance trap, whatever the backend it ran on.
 *
 * A kind's number never changes and is never reused, but any minor release may add kinds, so a host
 * handles kinds it doesn't know. Of Stasis's own kinds, so far only ::STASIS_TRAP_KIND_HOST_ABORT
 * and ::STASIS_TRAP_KIND_BOUNDARY_VIOLATION can happen: the others come with strings, handles and
 * the system interface, which this release lacks.
 */
typedef enum stasis_trap_kind {
    /**
     * The script ran `unreachable`, as a C script's `abort()`, failed `assert` and
     * `__builtin_trap()` do.
     */
    STASIS_TRAP_KIND_UNREACHABLE = 0,
    /**
     * The script read or wrote linear memory outside its bounds, as a C script that overflows its
     * stack does.
     */
    STASIS_TRAP_KIND_MEMORY_OUT_OF_BOUNDS = 1,
    /**
     * An indirect call failed: its index was outside the table, the table's element there held no
     * function, or held one of another type than the call expects.
     */
    STASIS_TRAP_KIND_BAD_INDIRECT_CALL = 2,
    /** The script divided an integer by zero, or took the remainder of dividing by zero. */
    STASIS_TRAP_KIND_INTEGER_DIVIDE_BY_ZERO = 3,
    /**
     * An integer division overflowed, or a float was converted to an integer type too small for it.
     */
    STASIS_TRAP_KIND_INTEGER_OVERFLOW = 4,
    /** A NaN was converted to an integer. */
    STASIS_TRAP_KIND_INVALID_CONVERSION = 5,
    /**
     * The script's calls nested too deep for its instance's stack, or the native stack of the
     * thread that called it ran short, which Stasis checks as a call enters the script and before
     * each host function.
     */
    STASIS_TRAP_KIND_STACK_EXHAUSTED = 6,
    /** Any other trap of WebAssembly's, such as an access to a table outside its bounds. */
    STASIS_TRAP_KIND_OTHER = 7,
    /** A host function or hook aborted the call. */
    STASIS_TRAP_KIND_HOST_ABORT = 8,
    /**
     * The script broke a rule of the boundary between it and the host, such as by passing a string
     * that isn't UTF-8, a handle it was never given, or arguments outside its memory.
     */
    STASIS_TRAP_KIND_BOUNDARY_VIOLATION = 9,
    /**
     * Stasis needed memory in the instance to hand the script a value, and the script's allocator
     * had none.
     */
    STASIS_TRAP_KIND_ALLOCATION_FAILED = 10,
    /** The instance needed a handle for a host object, and had none left. */
    STASIS_TRAP_KIND_HANDLES_EXHAUSTED = 11,
    /** The script exited, as a C script's `exit` does. */
    STASIS_TRAP_KIND_EXIT = 12,
    /** Not a kind. It makes the type 32 bits wide, as ::STASIS_STATUS_MAX_ENUM does. */
    STASIS_TRAP_KIND_MAX_ENUM = 0x7FFFFFFF,
} stasis_trap_kind;

/**
 * @brief The module offset of a frame whose backend can't say where in the module it was.
 */
#define STASIS_NO_OFFSET UINT32_MAX

/**
 * @brief One frame of a trapped call's stack: a function that was running when the call trapped.
 *
 * A trap in a call to a host function, such as an abort, has the host function as its innermost
 * frame.
 */
typedef struct stasis_trap_frame {
    /**
     * The function's index in the module's function index space, in which its imports come first.
     */
    uint32_t function_index;
    /**
     * The function's name in the module's name section, as NUL-terminated UTF-8, or null when the
     * module has no name section or the section doesn't name it. A host function, which the module
     * imports, has no name here.
     */
    const char *function_name;
    /**
     * Where in the module's bytes the instruction the function was at starts: in the innermost
     * frame, the instruction that trapped, and in every other, its call to the frame inside it.
     * ::STASIS_NO_OFFSET when the backend can't say, as WAMR's fast interpreter never can, and for
     * a host function, whose code isn't in the module.
     */
    uint32_t module_offset;
} stasis_trap_frame;

/**
 * @brief A trap report: how a script instance trapped.
 */
typedef struct stasis_trap {
    /** What made the instance trap. */
    stasis_trap_kind kind;
    /**
     * What happened, as NUL-terminated UTF-8: the backend's description of a trap of WebAssembly's
     * own, and the host's message for ::STASIS_TRAP_KIND_HOST_ABORT. Every other message is in
     * English, and its wording may change in any release.
     */
    const char *message;
    /** The number of frames. */
    size_t frame_count;
    /** The trapped call's stack, innermost frame first, or null when there are no frames. */
    const stasis_trap_frame *frames;
} stasis_trap;

/**
 * @brief The trap report of a script instance that has trapped.
 *
 * Starting an instance or calling one of its entry points can trap, and then returns ::STASIS_TRAP.
 * A trap ends the instance for good: every later start or call is refused with
 * ::STASIS_REFUSED_TRAPPED, and it can only have its report read, or be destroyed.
 *
 * @param instance The script instance.
 * @return Its trap report, which stays valid and unchanged until the instance is destroyed, or null
 * when it hasn't trapped, or `instance` is null.
 */
STASIS_API const stasis_trap *stasis_instance_trap(const stasis_instance *instance);

/**
 * @brief Ends the call into a script instance that a host function is part of, as a trap.
 *
 * A host function calls it on the instance that called it, which is running on the same thread.
 * The host function then returns as it would otherwise, and Stasis ignores its result. The call
 * traps as ::STASIS_TRAP_KIND_HOST_ABORT, with the message in its trap report, so the instance has
 * trapped for good. Aborting again in the same call changes nothing: the first message stays.
 *
 * @param instance The script instance whose call to end.
 * @param message Why, as NUL-terminated UTF-8, which Stasis copies.
 * @return ::STASIS_OK once the call is to end. ::STASIS_OUT_OF_MEMORY when there was no memory to
 * copy the message: the call still ends, but its trap report lacks the message.
 * ::STASIS_REFUSED_NOT_RUNNING when the instance isn't running on this thread, which changes
 * nothing, and ::STASIS_INVALID_ARGUMENT when `instance` or `message` is null.
 */
STASIS_API stasis_status stasis_abort(stasis_instance *instance, const char *message);

#ifdef __cplusplus
}
#endif

#endif
