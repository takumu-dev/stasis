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
} stasis_status;

/**
 * @brief Describes a status code in English.
 *
 * @param status Any value, a code this release doesn't know included.
 * @return A fixed description of the code, or "unknown status" for a value that isn't one. It's
 * never null.
 */
STASIS_API const char *stasis_status_text(stasis_status status);

#ifdef __cplusplus
}
#endif

#endif
