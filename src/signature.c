#include "signature.h"

#include "sha256.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How the encoding spells each scalar, in the order of stasis_signature_scalar. These spellings are
 * the encoding's own, and never follow the interface format's. */
static const char *const scalar_names[] = {
    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64", "bool", "string",
};

/* Adds text to the encoding: to its hash, and to its text, as much as fits there. */
static void append(stasis_signature *signature, const char *text) {
    const size_t length = strlen(text);
    stasis_sha256_update(&signature->sha, text, length);
    if (signature->capacity > 0) {
        const size_t room = signature->capacity - 1;
        if (signature->length < room) {
            const size_t fits = room - signature->length;
            memcpy(signature->text + signature->length, text, length < fits ? length : fits);
        }
        const size_t end = signature->length + length;
        signature->text[end < room ? end : room] = '\0';
    }
    signature->length += length;
}

/* Starts the next type or field, after a comma if one comes before it. */
static void separate(stasis_signature *signature) {
    if (signature->comma) {
        append(signature, ",");
    }
}

void stasis_signature_init(stasis_signature *signature, char *text, size_t capacity) {
    stasis_sha256_init(&signature->sha);
    signature->text = text;
    signature->capacity = capacity;
    signature->length = 0;
    signature->parameters_ended = false;
    if (capacity > 0) {
        text[0] = '\0';
    }
    append(signature, "(");
    signature->comma = false;
}

void stasis_signature_add_scalar(stasis_signature *signature, stasis_signature_scalar scalar) {
    separate(signature);
    append(signature, scalar_names[scalar]);
    signature->comma = true;
}

void stasis_signature_add_handle(stasis_signature *signature, const char *group,
                                 const char *object) {
    separate(signature);
    append(signature, "handle(");
    append(signature, group);
    append(signature, ".");
    append(signature, object);
    append(signature, ")");
    signature->comma = true;
}

void stasis_signature_begin_record(stasis_signature *signature) {
    separate(signature);
    append(signature, "record{");
    signature->comma = false;
}

void stasis_signature_begin_field(stasis_signature *signature, const char *name) {
    separate(signature);
    append(signature, name);
    append(signature, ":");
    signature->comma = false;
}

void stasis_signature_end_record(stasis_signature *signature) {
    append(signature, "}");
    signature->comma = true;
}

void stasis_signature_begin_result(stasis_signature *signature) {
    append(signature, ")->");
    signature->parameters_ended = true;
    signature->comma = false;
}

size_t stasis_signature_finish(stasis_signature *signature,
                               char hash[STASIS_SIGNATURE_HASH_LENGTH + 1]) {
    if (!signature->parameters_ended) {
        append(signature, ")");
    }
    uint8_t digest[STASIS_SHA256_SIZE];
    stasis_sha256_final(&signature->sha, digest);
    const char *digits = "0123456789abcdef";
    for (size_t i = 0; i < STASIS_SIGNATURE_HASH_LENGTH / 2; i++) {
        hash[2 * i] = digits[digest[i] >> 4];
        hash[(2 * i) + 1] = digits[digest[i] & 0xf];
    }
    hash[STASIS_SIGNATURE_HASH_LENGTH] = '\0';
    return signature->length;
}
