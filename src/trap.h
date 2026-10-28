/* Trap reports: how a trapped instance trapped, which it keeps until it's destroyed. */

#ifndef STASIS_TRAP_H
#define STASIS_TRAP_H

#include <stasis.h>

#include "module.h"

#include <wasm_export.h>

/* A trap report, and the memory its message and frames are in, which is null when there was none
 * to give them. */
struct trap_record {
    stasis_trap report;
    void *memory;
};

/* Fills in `record` with how WAMR says the last call into `instance` trapped. `code` is where the
 * instance's module has its code, which turns where WAMR says each frame was into the instruction
 * it was at. */
void stasis_trap_record_init(struct trap_record *record, wasm_module_inst_t instance,
                             const struct module_code *code);

/* Frees what filling in `record` allocated. */
void stasis_trap_record_free(struct trap_record *record);

#endif
