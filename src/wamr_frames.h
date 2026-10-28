/* The call stack that WAMR records when a call into an instance traps, which its API only prints.
 * wamr_frames.c reads WAMR's own record of it, so it's compiled with WAMR's settings and its
 * internal headers, and has to be checked again whenever WAMR is upgraded. */

#ifndef STASIS_WAMR_FRAMES_H
#define STASIS_WAMR_FRAMES_H

#include <stdbool.h>
#include <stdint.h>

/* An instance, as WAMR's API hands it out: what wasm_module_inst_t points at. This header names it
 * without including WAMR's API, because with MSVC, wamr_frames.c has to include WAMR's internal
 * headers before the API. They declare wasm_runtime_malloc and wasm_runtime_free as dllexport, and
 * MSVC refuses a declaration that adds dllexport to a function declared without it. */
/* NOLINTNEXTLINE(readability-identifier-naming): it's WAMR's name for the type */
struct WASMModuleInstanceCommon;

/* One frame of the call stack, as WAMR recorded it. */
struct wamr_frame {
    /* The function's index in the module's function index space. */
    uint32_t function_index;
    /* The function's name in the module's name section, or null. It's WAMR's, and lasts as long as
     * the module stays loaded. */
    const char *function_name;
    /* With the classic interpreter, the offset in the module's bytes just past the instruction the
     * function was at: the one that trapped, or the call to the frame inside it. It's 0 when WAMR
     * doesn't know it, and always with the fast interpreter, whose frames hold offsets into code of
     * its own. */
    uint32_t end_offset;
};

/* Whether WAMR's frames say where in the module each function was, which only its classic
 * interpreter's do. */
bool stasis_wamr_frames_have_offsets(void);

/* The number of frames WAMR recorded when the last call into `instance` trapped. */
uint32_t stasis_wamr_frame_count(struct WASMModuleInstanceCommon *instance);

/* Reads the frame at `index` of those WAMR recorded, counting from the innermost. */
void stasis_wamr_read_frame(struct WASMModuleInstanceCommon *instance, uint32_t index,
                            struct wamr_frame *frame);

#endif
