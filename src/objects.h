/* The C API's three objects, which the core's sources share: a library, the scripts loaded into
 * it, and their instances. Each object's count of children changes only under Stasis's lock. */

#ifndef STASIS_OBJECTS_H
#define STASIS_OBJECTS_H

#include <stasis.h>
#include <stasis_interface.h>

#include "handles.h"
#include "host_functions.h"
#include "layout.h"
#include "module.h"
#include "trap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wasm_export.h>

/* The export that initialises a new instance, which Stasis calls when it starts. */
#define STASIS_INITIALIZE_NAME "stasis_initialize"

/* The export through which Stasis allocates in a script's memory. */
#define STASIS_REALLOC_NAME "cabi_realloc"

/* The most pages of linear memory an instance may have, whatever its module allows. WAMR passes
 * sizes to its allocator in 32 bits, which 65,536 pages would overflow. */
enum { STASIS_MAX_MEMORY_PAGES = 65535 };

struct stasis_library {
    /* The host's settings, as it gave them. */
    stasis_library_settings settings;
    /* The size of each instance's stack, with the default in place of 0. */
    uint32_t stack_size;
    /* The number of the library's scripts that haven't been destroyed. */
    size_t script_count;
};

struct stasis_script {
    stasis_library *library;
    const stasis_interface *interface_data;
    uint8_t identity[STASIS_IDENTITY_SIZE];
    /* Stasis's copy of the module's bytes, which WAMR loaded from. WAMR writes into it and keeps
     * pointers into it, so it's freed only once the module is unloaded. */
    uint8_t *bytes;
    wasm_module_t module;
    /* The export name of each of the interface's entry points, as a C string, in the order of
     * their ids, or null for one the script doesn't provide. */
    char **entry_points;
    size_t entry_point_count;
    /* The name of each entry point's cabi_post_*, by id, or null for one the script doesn't
     * export. */
    char **post_returns;
    /* How each of the interface's entry points' values cross, by id. */
    struct function_layout *entry_point_layouts;
    /* Whether the script exports stasis_initialize. */
    bool initializes;
    /* Why the script isn't snapshottable, one problem a line, or null when it is. */
    char *unsnapshottable;
    /* Where the module's code is, which turns where WAMR says a frame of a trap report was into the
     * instruction it was at. It's empty when WAMR doesn't say. */
    struct module_code code;
    /* The copy of the interface's host functions that WAMR linked the module's imports to. */
    struct host_functions host_functions;
    /* The number of the script's instances that haven't been destroyed. */
    size_t instance_count;
};

/* Which of the canonical ABI's functions Stasis is inside of, in a running instance, during which
 * the script may call no import. */
enum inside {
    INSIDE_NEITHER,
    INSIDE_REALLOC,
    INSIDE_POST_RETURN,
};

/* Where an instance is in its life. It's created, then ready once it has started, and running
 * during each call into it, until a call traps, which leaves it trapped for good. */
enum instance_state {
    INSTANCE_CREATED,
    INSTANCE_READY,
    INSTANCE_RUNNING,
    INSTANCE_TRAPPED,
};

struct stasis_instance {
    stasis_script *script;
    void *user_data;
    enum instance_state state;
    wasm_module_inst_t module_instance;
    wasm_exec_env_t exec_env;
    /* stasis_initialize, or null when the script doesn't export it. */
    wasm_function_inst_t initialize;
    /* The function behind each entry point, by id, or null for one the script doesn't provide. */
    wasm_function_inst_t *entry_points;
    /* cabi_realloc, or null when the script doesn't export it. */
    wasm_function_inst_t reallocate;
    /* The cabi_post_* of each entry point, by id, or null for one the script doesn't export. */
    wasm_function_inst_t *post_returns;
    /* Which of those Stasis is inside of, and for a cabi_post_*, its entry point. */
    enum inside inside;
    const stasis_entry_point *post_returning;
    /* The buffer that the strings in the last call's result are copied into, and its size. */
    unsigned char *results;
    size_t results_size;
    /* The room for a host function's arguments and result, while the instance calls one, which is
     * null when its script's interface has no host function Stasis calls. */
    unsigned char *scratch;
    /* The handle the instance has for each host object it has been given. */
    struct handle_table handles;
    /* The trap of Stasis's own that the call the instance is running has come to, if any. */
    struct raised_trap raised;
    /* The trap report, once the instance has trapped. */
    struct trap_record trap;
};

#endif
