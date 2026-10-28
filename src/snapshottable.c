#include "snapshottable.h"

#include "message.h"
#include "module.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The problems with the module's memories: it must define one, export it, and not share it. It
 * imports none, as no script may. */
static void explain_memories(const struct module *module, struct message *reason) {
    if (module->memory_count == 0) {
        stasis_message_add(reason, "the script defines no memory");
        return;
    }
    if (module->memory_count > 1) {
        stasis_message_add(reason, "the script defines %lu memories, not one",
                           (unsigned long)module->memory_count);
    }
    for (uint32_t i = 0; i < module->memory_count; i++) {
        const uint32_t index = module->memory_import_count + i;
        if (!module->memories[i].exported) {
            stasis_message_add(reason, "memory %lu isn't exported", (unsigned long)index);
        }
        if (module->memories[i].shared) {
            stasis_message_add(reason, "memory %lu is shared", (unsigned long)index);
        }
    }
}

/* The problems with the module's code: each instruction that changes a table or drops a segment,
 * named once, with the first function that contains it. */
static void explain_changes(const struct module *module, struct message *reason) {
    static const char *const names[MODULE_CHANGE_COUNT] = {
        "table.set",  "table.grow", "table.fill", "table.copy",
        "table.init", "elem.drop",  "data.drop",
    };
    for (size_t change = 0; change < MODULE_CHANGE_COUNT; change++) {
        const struct module_first_use *use = &module->first_uses[change];
        if (use->used) {
            stasis_message_add(reason, "the script contains `%s`, first in function %lu",
                               names[change], (unsigned long)use->function);
        }
    }
}

/* Whether a snapshot can hold a value of a type: a number, rather than a reference, which means
 * something only to the backend that made it. */
static bool is_number(uint8_t type) {
    switch (type) {
    case MODULE_I32:
    case MODULE_I64:
    case MODULE_F32:
    case MODULE_F64:
        return true;
    default:
        return false;
    }
}

/* The problems with the module's globals: each mutable one must be exported, and a number. */
static void explain_globals(const struct module *module, struct message *reason) {
    for (uint32_t i = 0; i < module->global_count; i++) {
        const struct module_global *global = &module->globals[i];
        const uint32_t index = module->global_import_count + i;
        if (!global->is_mutable) {
            continue;
        }
        if (!global->exported) {
            stasis_message_add(reason, "global %lu is mutable but not exported",
                               (unsigned long)index);
        }
        if (!is_number(global->type)) {
            stasis_message_add(reason,
                               "global %lu is mutable but of type %s, not i32, i64, f32 or f64",
                               (unsigned long)index, stasis_module_type_name(global->type));
        }
    }
}

void stasis_explain_unsnapshottable(const struct module *module, struct message *reason) {
    explain_memories(module, reason);
    explain_changes(module, reason);
    explain_globals(module, reason);
}
