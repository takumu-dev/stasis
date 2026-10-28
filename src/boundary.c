#include "boundary.h"

#include <stasis_interface.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wasm_export.h>

bool stasis_is_scalar(const stasis_value *value) {
    switch (value->type) {
    case STASIS_TYPE_U8:
    case STASIS_TYPE_U16:
    case STASIS_TYPE_U32:
    case STASIS_TYPE_U64:
    case STASIS_TYPE_S8:
    case STASIS_TYPE_S16:
    case STASIS_TYPE_S32:
    case STASIS_TYPE_S64:
    case STASIS_TYPE_F32:
    case STASIS_TYPE_F64:
    case STASIS_TYPE_BOOL:
        return true;
    case STASIS_TYPE_STRING:
    case STASIS_TYPE_RECORD:
    case STASIS_TYPE_HANDLE:
    case STASIS_TYPE_MAX_ENUM:
        break;
    }
    return false;
}

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
