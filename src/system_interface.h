/* The system interface: the 46 functions of WASI preview 1, which every script may import from
 * `wasi_snapshot_preview1` beside its interface's host functions, and which Stasis answers itself.
 * It keeps nothing of an instance's on the host's side, so whatever a script reaches through it is
 * in the script's memory, and so in its snapshots. A script reaches nothing outside its sandbox
 * through it but what the host gives: its output goes to the library's output hook, and the time
 * and random bytes come from its clock and entropy hooks. Everything else fails, and changes
 * nothing. */

#ifndef STASIS_SYSTEM_INTERFACE_H
#define STASIS_SYSTEM_INTERFACE_H

#include <stasis.h>

#include "descriptors.h"

#include <stdbool.h>

/* The module that a script imports the system interface from. */
#define STASIS_SYSTEM_INTERFACE_MODULE "wasi_snapshot_preview1"

/* Sets `type` to the core function type of the system interface's function called `name`. Returns
 * false if the system interface has none. */
bool stasis_system_function_type(stasis_string name, struct core_type *type);

/* Registers the system interface's functions with WAMR, under Stasis's lock, once WAMR has been
 * initialised. WAMR forgets them when it's torn down. Returns false if WAMR had no memory for
 * them. */
bool stasis_system_interface_register(void);

#endif
