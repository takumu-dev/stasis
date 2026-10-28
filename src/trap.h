/* Trap reports: how a trapped instance trapped, which it keeps until it's destroyed. */

#ifndef STASIS_TRAP_H
#define STASIS_TRAP_H

#include <stasis.h>

#include "message.h"
#include "module.h"

#include <stdbool.h>
#include <wasm_export.h>

/* A trap report, whether it has been filled in, and the memory its message and frames are in,
 * which is null when there was none to give them. */
struct trap_record {
    stasis_trap report;
    bool filled;
    void *memory;
};

/* A trap of Stasis's own that a call has come to, such as a host function's abort or an argument
 * outside the script's memory, until the call has returned. Stasis raises it, then has WAMR unwind
 * the call, so the trap report takes its kind and message from here rather than from WAMR. */
struct raised_trap {
    bool raised;
    stasis_trap_kind kind;
    /* The message, or null if there was no memory to keep it. */
    char *message;
};

/* Raises a trap of `kind`, with a message formatted as by snprintf, unless the call has raised one
 * already, whose message wins. Returns false if there was no memory to keep the message, which the
 * trap report then lacks. */
bool stasis_raise_trap(struct raised_trap *raised, stasis_trap_kind kind, const char *format, ...)
    STASIS_PRINTF_FORMAT(3, 4);

/* Forgets the trap that was raised, if one was, once the call has returned. */
void stasis_raised_trap_clear(struct raised_trap *raised);

/* Fills in `record` with how the last call into `instance` trapped: as `raised` says, if a trap of
 * Stasis's own was raised, and otherwise as WAMR says. The frames are WAMR's either way. `code` is
 * where the instance's module has its code, which turns where WAMR says each frame was into the
 * instruction it was at. */
void stasis_trap_record_init(struct trap_record *record, wasm_module_inst_t instance,
                             const struct module_code *code, const struct raised_trap *raised);

/* Frees what filling in `record` allocated. */
void stasis_trap_record_free(struct trap_record *record);

#endif
