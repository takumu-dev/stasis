#include "wamr_frames.h"

#include <bh_vector.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wasm.h>
#include <wasm_export.h>
#include <wasm_runtime.h>

/* When a call traps, the interpreter records each frame on the instance's stack in the instance's
 * `frames`, innermost first, as a WASMCApiFrame. The frame's func_offset is where its instruction
 * pointer was: with the classic interpreter, an offset in the module's bytes, which the loader
 * runs the code from, and with the fast interpreter, one in the code it compiles each function
 * into. The frame's func_name_wp falls back on an export's or an import's name, so a frame's name
 * is read from the function itself, where only the name section puts one. */

bool stasis_wamr_frames_have_offsets(void) { return WASM_ENABLE_FAST_INTERP == 0; }

/* The interpreter's own view of an instance. WAMR hands out each instance as its common type, but
 * with only an interpreter, every one is the interpreter's, as WAMR allocated it, so it's as
 * aligned as the interpreter's type needs. */
static const WASMModuleInstance *interpreter_instance(wasm_module_inst_t instance) {
    const void *allocated = instance;
    return allocated;
}

uint32_t stasis_wamr_frame_count(wasm_module_inst_t instance) {
    const WASMModuleInstance *module_instance = interpreter_instance(instance);
    if (module_instance->frames == NULL) {
        return 0;
    }
    /* WAMR counts the frames it records in 32 bits. */
    return (uint32_t)bh_vector_size(module_instance->frames);
}

void stasis_wamr_read_frame(wasm_module_inst_t instance, uint32_t index, struct wamr_frame *frame) {
    const WASMModuleInstance *module_instance = interpreter_instance(instance);
    const WASMModule *module = module_instance->module;
    WASMCApiFrame recorded;
    memset(&recorded, 0, sizeof recorded);
    (void)bh_vector_get(module_instance->frames, index, &recorded);
    frame->function_index = recorded.func_index;
    frame->function_name = NULL;
    if (recorded.func_index >= module->import_function_count) {
        const uint32_t defined = recorded.func_index - module->import_function_count;
        if (defined < module->function_count) {
            frame->function_name = module->functions[defined]->field_name;
        }
    }
    frame->end_offset = stasis_wamr_frames_have_offsets() ? recorded.func_offset : 0;
}
