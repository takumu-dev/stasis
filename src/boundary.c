#include "boundary.h"

#include <stasis.h>
#include <stasis_interface.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wasm_export.h>

/* The i32 with the same 32 bits as `bits`. */
static int32_t i32_of(uint32_t bits) {
    int32_t value = 0;
    memcpy(&value, &bits, sizeof value);
    return value;
}

void stasis_lower_scalar(const stasis_value *value, const void *source, wasm_val_t *core) {
    const unsigned char *at = (const unsigned char *)source + value->offset;
    memset(core, 0, sizeof *core);
    core->kind = WASM_I32;
    switch (value->type) {
    case STASIS_TYPE_U8: {
        uint8_t scalar = 0;
        memcpy(&scalar, at, sizeof scalar);
        core->of.i32 = scalar;
        break;
    }
    case STASIS_TYPE_U16: {
        uint16_t scalar = 0;
        memcpy(&scalar, at, sizeof scalar);
        core->of.i32 = scalar;
        break;
    }
    case STASIS_TYPE_U32: {
        uint32_t scalar = 0;
        memcpy(&scalar, at, sizeof scalar);
        core->of.i32 = i32_of(scalar);
        break;
    }
    case STASIS_TYPE_S8: {
        /* Read as its bits, then extended by its sign. */
        uint8_t bits = 0;
        memcpy(&bits, at, sizeof bits);
        core->of.i32 = (int32_t)(bits ^ 0x80U) - 0x80;
        break;
    }
    case STASIS_TYPE_S16: {
        int16_t scalar = 0;
        memcpy(&scalar, at, sizeof scalar);
        core->of.i32 = scalar;
        break;
    }
    case STASIS_TYPE_S32:
        memcpy(&core->of.i32, at, sizeof core->of.i32);
        break;
    case STASIS_TYPE_BOOL: {
        bool scalar = false;
        memcpy(&scalar, at, sizeof scalar);
        core->of.i32 = scalar ? 1 : 0;
        break;
    }
    case STASIS_TYPE_U64:
    case STASIS_TYPE_S64:
        core->kind = WASM_I64;
        memcpy(&core->of.i64, at, sizeof core->of.i64);
        break;
    case STASIS_TYPE_F32:
        core->kind = WASM_F32;
        memcpy(&core->of.f32, at, sizeof core->of.f32);
        break;
    case STASIS_TYPE_F64:
        core->kind = WASM_F64;
        memcpy(&core->of.f64, at, sizeof core->of.f64);
        break;
    case STASIS_TYPE_STRING:
    case STASIS_TYPE_RECORD:
    case STASIS_TYPE_HANDLE:
    case STASIS_TYPE_MAX_ENUM:
        break;
    }
}

void stasis_lift_scalar(const stasis_value *value, const wasm_val_t *core, void *destination) {
    unsigned char *at = (unsigned char *)destination + value->offset;
    const uint32_t bits = (uint32_t)core->of.i32;
    switch (value->type) {
    case STASIS_TYPE_U8: {
        const uint8_t scalar = (uint8_t)(bits & 0xff);
        memcpy(at, &scalar, sizeof scalar);
        break;
    }
    case STASIS_TYPE_U16: {
        const uint16_t scalar = (uint16_t)(bits & 0xffff);
        memcpy(at, &scalar, sizeof scalar);
        break;
    }
    case STASIS_TYPE_S8: {
        /* The low 8 bits, read as two's complement. */
        const int8_t scalar = (int8_t)((int32_t)((bits & 0xff) ^ 0x80) - 0x80);
        memcpy(at, &scalar, sizeof scalar);
        break;
    }
    case STASIS_TYPE_S16: {
        const int16_t scalar = (int16_t)((int32_t)((bits & 0xffff) ^ 0x8000) - 0x8000);
        memcpy(at, &scalar, sizeof scalar);
        break;
    }
    case STASIS_TYPE_U32:
        memcpy(at, &bits, sizeof bits);
        break;
    case STASIS_TYPE_S32:
        memcpy(at, &core->of.i32, sizeof core->of.i32);
        break;
    case STASIS_TYPE_BOOL: {
        const bool scalar = bits != 0;
        memcpy(at, &scalar, sizeof scalar);
        break;
    }
    case STASIS_TYPE_U64:
    case STASIS_TYPE_S64:
        memcpy(at, &core->of.i64, sizeof core->of.i64);
        break;
    case STASIS_TYPE_F32:
        memcpy(at, &core->of.f32, sizeof core->of.f32);
        break;
    case STASIS_TYPE_F64:
        memcpy(at, &core->of.f64, sizeof core->of.f64);
        break;
    case STASIS_TYPE_STRING:
    case STASIS_TYPE_RECORD:
    case STASIS_TYPE_HANDLE:
    case STASIS_TYPE_MAX_ENUM:
        break;
    }
}

/* The core value type that a scalar lowers to. */
static wasm_valkind_t core_kind(const stasis_value *value) {
    switch (value->type) {
    case STASIS_TYPE_U64:
    case STASIS_TYPE_S64:
        return WASM_I64;
    case STASIS_TYPE_F32:
        return WASM_F32;
    case STASIS_TYPE_F64:
        return WASM_F64;
    case STASIS_TYPE_U8:
    case STASIS_TYPE_U16:
    case STASIS_TYPE_U32:
    case STASIS_TYPE_S8:
    case STASIS_TYPE_S16:
    case STASIS_TYPE_S32:
    case STASIS_TYPE_BOOL:
    case STASIS_TYPE_STRING:
    case STASIS_TYPE_RECORD:
    case STASIS_TYPE_HANDLE:
    case STASIS_TYPE_MAX_ENUM:
        break;
    }
    return WASM_I32;
}

/* The number of bytes of a slot that a core value of `kind` takes, from the slot's start. */
static size_t slot_size(wasm_valkind_t kind) {
    switch (kind) {
    case WASM_I64:
        return sizeof(int64_t);
    case WASM_F32:
        return sizeof(float);
    case WASM_F64:
        return sizeof(double);
    default:
        return sizeof(int32_t);
    }
}

void stasis_lift_raw_scalar(const stasis_value *value, const uint64_t *slot, void *destination) {
    wasm_val_t core;
    memset(&core, 0, sizeof core);
    core.kind = core_kind(value);
    memcpy(&core.of, slot, slot_size(core.kind));
    stasis_lift_scalar(value, &core, destination);
}

void stasis_lower_raw_scalar(const stasis_value *value, const void *source, uint64_t *slot) {
    wasm_val_t core;
    stasis_lower_scalar(value, source, &core);
    *slot = 0;
    memcpy(slot, &core.of, slot_size(core.kind));
}

size_t stasis_memory_size(const stasis_value *value) {
    switch (value->type) {
    case STASIS_TYPE_U16:
    case STASIS_TYPE_S16:
        return 2;
    case STASIS_TYPE_U32:
    case STASIS_TYPE_S32:
    case STASIS_TYPE_F32:
    case STASIS_TYPE_HANDLE:
        return 4;
    case STASIS_TYPE_U64:
    case STASIS_TYPE_S64:
    case STASIS_TYPE_F64:
    case STASIS_TYPE_STRING:
        return 8;
    case STASIS_TYPE_U8:
    case STASIS_TYPE_S8:
    case STASIS_TYPE_BOOL:
    case STASIS_TYPE_RECORD:
    case STASIS_TYPE_MAX_ENUM:
        break;
    }
    return 1;
}

size_t stasis_memory_alignment(const stasis_value *value) {
    return value->type == STASIS_TYPE_STRING ? 4 : stasis_memory_size(value);
}

uint32_t stasis_load_u32(const unsigned char *at) {
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

void stasis_store_u32(unsigned char *at, uint32_t value) {
    for (size_t i = 0; i < 4; i++) {
        at[i] = (unsigned char)(value >> (8 * i) & 0xff);
    }
}

struct script_memory stasis_script_memory(wasm_module_inst_t module_instance) {
    struct script_memory memory = {NULL, 0};
    wasm_memory_inst_t default_memory = wasm_runtime_get_default_memory(module_instance);
    if (default_memory != NULL) {
        memory.base = wasm_memory_get_base_address(default_memory);
        memory.size = wasm_memory_get_cur_page_count(default_memory) *
                      wasm_memory_get_bytes_per_page(default_memory);
    }
    return memory;
}

/* The number of bytes in the character that the `size` bytes at `bytes` start with, which is at
 * least 1, or 0 if they don't start with a whole character of UTF-8. */
static size_t character_length(const unsigned char *bytes, size_t size) {
    const unsigned char lead = bytes[0];
    if (lead < 0x80) {
        return 1;
    }
    /* The character's length, by its lead byte, and the range its second byte is in, which rules
     * out overlong encodings, surrogates and what's past U+10FFFF. Every later byte is a
     * continuation byte. */
    size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xbf;
    if (lead >= 0xc2 && lead <= 0xdf) {
        length = 2;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        length = 3;
        low = lead == 0xe0 ? 0xa0 : low;
        high = lead == 0xed ? 0x9f : high;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        length = 4;
        low = lead == 0xf0 ? 0x90 : low;
        high = lead == 0xf4 ? 0x8f : high;
    } else {
        return 0;
    }
    if (size < length || bytes[1] < low || bytes[1] > high) {
        return 0;
    }
    for (size_t i = 2; i < length; i++) {
        if ((bytes[i] & 0xc0) != 0x80) {
            return 0;
        }
    }
    return length;
}

size_t stasis_utf8_length(const unsigned char *bytes, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const size_t length = character_length(bytes + offset, size - offset);
        if (length == 0) {
            return offset;
        }
        offset += length;
    }
    return size;
}

enum string_fault stasis_check_host_string(stasis_string string, size_t *valid) {
    if (string.size == 0) {
        return STRING_CROSSES;
    }
    if (string.data == NULL) {
        return STRING_DATA_NULL;
    }
    if (string.size > STASIS_MAX_STRING_SIZE) {
        return STRING_TOO_LARGE;
    }
    *valid = stasis_utf8_length((const unsigned char *)string.data, string.size);
    return *valid < string.size ? STRING_NOT_UTF8 : STRING_CROSSES;
}

void stasis_load_scalar(const stasis_value *value, const unsigned char *at, void *destination) {
    /* The scalar's bytes, little-endian, as a core value of its own size. */
    const size_t size = stasis_memory_size(value);
    uint64_t bits = 0;
    for (size_t i = size; i-- > 0;) {
        bits = (bits << 8) | at[i];
    }
    wasm_val_t core;
    memset(&core, 0, sizeof core);
    core.kind = core_kind(value);
    if (size == 8) {
        memcpy(&core.of, &bits, sizeof bits);
    } else {
        const uint32_t narrow = (uint32_t)bits;
        memcpy(&core.of, &narrow, sizeof narrow);
    }
    stasis_lift_scalar(value, &core, destination);
}

void stasis_store_scalar(const stasis_value *value, const void *source, unsigned char *at) {
    /* The scalar as a core value of its own size, stored little-endian. */
    wasm_val_t core;
    stasis_lower_scalar(value, source, &core);
    uint64_t bits = 0;
    if (slot_size(core.kind) == sizeof bits) {
        memcpy(&bits, &core.of, sizeof bits);
    } else {
        uint32_t narrow = 0;
        memcpy(&narrow, &core.of, sizeof narrow);
        bits = narrow;
    }
    const size_t size = stasis_memory_size(value);
    for (size_t i = 0; i < size; i++) {
        at[i] = (unsigned char)(bits >> (8 * i) & 0xff);
    }
}
