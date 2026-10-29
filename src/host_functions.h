/* How scripts call their interface's host functions on WAMR.
 *
 * A script imports each host function from its group's module, by a field name that carries the
 * hash of its signature:
 *
 *     <function>:<hash>            a group's host function
 *     <object>.<method>:<hash>     a method of one of the group's kinds of host object
 *
 * as in `find:9f6e056c86b04946` from `acme:dungeon/world@1`. WAMR links a module's imports as it
 * loads it, to the host functions registered with it then, which are the process's. So each load
 * makes its own copy of the interface's names, with one trampoline for every host function, and
 * registers it for as long as WAMR loads, under Stasis's lock. The script keeps the copy until it's
 * destroyed.
 *
 * The trampoline lifts the script's arguments into the host function's argument struct, calls its
 * thunk, and lowers the result, as the host function's layout lays them out. A handle is lifted
 * into the host object it names in the calling instance, and a host object is lowered into the
 * instance's handle for it. */

#ifndef STASIS_HOST_FUNCTIONS_H
#define STASIS_HOST_FUNCTIONS_H

#include <stasis.h>
#include <stasis_interface.h>

#include "descriptors.h"
#include "layout.h"

#include <lib_export.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wasm_export.h>

/* A host function or method as a script links it: its descriptor, and how its values cross. The
 * trampoline that WAMR calls in its place has it as its attachment. */
struct linked_function {
    const stasis_host_function *descriptor;
    struct function_layout layout;
};

/* The host functions of one group, as WAMR links them: the group's module name, and a symbol for
 * each host function and method, which WAMR sorts in place. */
struct host_group {
    char *module;
    NativeSymbol *symbols;
    uint32_t symbol_count;
    /* The symbols' names, one after another. */
    char *names;
    /* What the symbols are attached to, in the order they were added. */
    struct linked_function *functions;
};

/* A script's copy of its interface's host functions, and the room an instance keeps for calling
 * one: for the largest argument struct, and after it, at `result_offset`, the largest result. */
struct host_functions {
    struct host_group *groups;
    size_t group_count;
    size_t scratch_size;
    size_t result_offset;
};

/* Makes the copy of the host functions of the interface that `layouts` lays out. Returns STASIS_OK,
 * STASIS_OUT_OF_MEMORY, or STASIS_INVALID_ARGUMENT if the interface's descriptors are malformed.
 * Whatever it returns, `functions` must be freed with stasis_host_functions_free. */
stasis_status stasis_host_functions_init(struct host_functions *functions,
                                         struct layout_builder *layouts);

/* Registers the host functions with WAMR, under Stasis's lock, for a load. Returns false if WAMR
 * had no memory for them, having registered none. */
bool stasis_host_functions_register(struct host_functions *functions);

/* Undoes stasis_host_functions_register, under Stasis's lock, once WAMR has loaded. */
void stasis_host_functions_unregister(struct host_functions *functions);

/* Frees the copy, once the module WAMR linked to it is unloaded. */
void stasis_host_functions_free(struct host_functions *functions);

#endif
