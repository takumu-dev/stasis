#include "trap.h"

#include <stasis.h>

#include "allocator.h"
#include "module.h"
#include "wamr_frames.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wasm_export.h>

/* What WAMR puts before its description of a trap. */
static const char description_prefix[] = "Exception: ";

/* The message of a trap report when there's no memory to copy the trap's own. */
static const char lost_message[] = "the trap's message is lost: there was no memory to keep it";

/* The kind of each trap that WAMR describes, by its description, which is all it says of a trap.
 * The pinned WAMR's descriptions are in its sources, and a test of each kind catches any change. */
static const struct {
    const char *message;
    stasis_trap_kind kind;
} wamr_kinds[] = {
    {"unreachable", STASIS_TRAP_KIND_UNREACHABLE},
    {"out of bounds memory access", STASIS_TRAP_KIND_MEMORY_OUT_OF_BOUNDS},
    /* An indirect call's index outside the table, an element that holds no function, and one that
     * holds a function of another type. */
    {"undefined element", STASIS_TRAP_KIND_BAD_INDIRECT_CALL},
    {"uninitialized element", STASIS_TRAP_KIND_BAD_INDIRECT_CALL},
    {"indirect call type mismatch", STASIS_TRAP_KIND_BAD_INDIRECT_CALL},
    {"integer divide by zero", STASIS_TRAP_KIND_INTEGER_DIVIDE_BY_ZERO},
    {"integer overflow", STASIS_TRAP_KIND_INTEGER_OVERFLOW},
    {"invalid conversion to integer", STASIS_TRAP_KIND_INVALID_CONVERSION},
    /* The interpreter's stack, and the native stack of the thread that calls the instance. */
    {"wasm operand stack overflow", STASIS_TRAP_KIND_STACK_EXHAUSTED},
    {"native stack overflow", STASIS_TRAP_KIND_STACK_EXHAUSTED},
};

/* The kind of the trap WAMR describes with `message`: STASIS_TRAP_KIND_OTHER for one it doesn't
 * know. */
static stasis_trap_kind wamr_kind(const char *message) {
    for (size_t i = 0; i < sizeof wamr_kinds / sizeof wamr_kinds[0]; i++) {
        if (strcmp(message, wamr_kinds[i].message) == 0) {
            return wamr_kinds[i].kind;
        }
    }
    return STASIS_TRAP_KIND_OTHER;
}

void stasis_trap_record_init(struct trap_record *record, wasm_module_inst_t instance,
                             const struct module_code *code) {
    const char *message = wasm_runtime_get_exception(instance);
    if (message == NULL) {
        message = "";
    }
    if (strncmp(message, description_prefix, sizeof description_prefix - 1) == 0) {
        message += sizeof description_prefix - 1;
    }
    stasis_trap *report = &record->report;
    report->kind = wamr_kind(message);
    report->message = lost_message;
    report->frame_count = 0;
    report->frames = NULL;

    /* The frames and the message share one block, the frames first. Without the memory for it,
     * the report keeps only its kind. */
    const uint32_t frame_count = stasis_wamr_frame_count(instance);
    const size_t frames_size = (size_t)frame_count * sizeof(stasis_trap_frame);
    const size_t message_size = strlen(message) + 1;
    record->memory = stasis_allocate(frames_size + message_size);
    if (record->memory == NULL) {
        return;
    }
    stasis_trap_frame *frames = record->memory;
    for (uint32_t i = 0; i < frame_count; i++) {
        struct wamr_frame recorded;
        stasis_wamr_read_frame(instance, i, &recorded);
        frames[i].function_index = recorded.function_index;
        frames[i].function_name = recorded.function_name;
        frames[i].module_offset =
            stasis_module_instruction_before(code, recorded.function_index, recorded.end_offset);
    }
    char *copy = (char *)record->memory + frames_size;
    memcpy(copy, message, message_size);
    report->message = copy;
    report->frame_count = frame_count;
    report->frames = frame_count > 0 ? frames : NULL;
}

void stasis_trap_record_free(struct trap_record *record) { stasis_deallocate(record->memory); }
