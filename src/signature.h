/* The encoding of signatures, whose hashes name a script's imports and entry points.
 *
 * A signature's encoding is compact ASCII text, with no whitespace, from this grammar:
 *
 *     signature = "(" [type *("," type)] ")" ["->" type]
 *     type      = "u8" | "u16" | "u32" | "u64" | "s8" | "s16" | "s32" | "s64"
 *               | "f32" | "f64" | "bool" | "string"
 *               | "handle(" group "." object ")"
 *               | "record{" name ":" type *("," name ":" type) "}"
 *
 * A method's handle is its first parameter, and entry points are encoded as host functions are. A
 * handle names its kind of host object by the group that declares it. The hash is the first 16
 * digits of the lowercase hexadecimal SHA-256 of the encoding. The encoding can never change, since
 * that would rename every import and entry point, and break every script built until then. It only
 * grows, as types arrive.
 *
 * A signature is written type by type, in order, and the encoder adds the punctuation between them:
 *
 *     stasis_signature signature;
 *     stasis_signature_init(&signature, NULL, 0);
 *     stasis_signature_add_scalar(&signature, STASIS_SIGNATURE_STRING);
 *     stasis_signature_begin_result(&signature);
 *     stasis_signature_add_handle(&signature, "world", "entity");
 *     stasis_signature_finish(&signature, hash);
 *
 * gives `(string)->handle(world.entity)`, which hashes to 9f6e056c86b04946.
 *
 * It depends on SHA-256 alone, so the library and the generator both compile it. */

#ifndef STASIS_SIGNATURE_H
#define STASIS_SIGNATURE_H

#include "sha256.h"

#include <stdbool.h>
#include <stddef.h>

/* The length of a hash, in hexadecimal digits. */
enum { STASIS_SIGNATURE_HASH_LENGTH = 16 };

/* The types that aren't records or handles. */
typedef enum stasis_signature_scalar {
    STASIS_SIGNATURE_U8,
    STASIS_SIGNATURE_U16,
    STASIS_SIGNATURE_U32,
    STASIS_SIGNATURE_U64,
    STASIS_SIGNATURE_S8,
    STASIS_SIGNATURE_S16,
    STASIS_SIGNATURE_S32,
    STASIS_SIGNATURE_S64,
    STASIS_SIGNATURE_F32,
    STASIS_SIGNATURE_F64,
    STASIS_SIGNATURE_BOOL,
    STASIS_SIGNATURE_STRING,
} stasis_signature_scalar;

/* A signature being encoded. */
typedef struct stasis_signature {
    /* The hash of the encoding so far. */
    stasis_sha256 sha;
    /* Where the encoding's text goes, if anywhere, and how many bytes fit there. */
    char *text;
    size_t capacity;
    /* The length of the encoding so far. */
    size_t length;
    /* Whether a comma comes before the next type or field. */
    bool comma;
    /* Whether the parameters have ended, so that the types that follow are the result. */
    bool parameters_ended;
} stasis_signature;

/* Starts a signature, before its first parameter. Its encoding goes into `text`, which has room for
 * `capacity` bytes, and may be null when `capacity` is 0. The text is cut short where it doesn't
 * fit, but stasis_signature_finish returns its whole length, and the hash covers all of it. */
void stasis_signature_init(stasis_signature *signature, char *text, size_t capacity);

/* Adds a scalar: a parameter, the result, or a record's field. */
void stasis_signature_add_scalar(stasis_signature *signature, stasis_signature_scalar scalar);

/* Adds a handle to the kind of host object `object`, which `group` declares. */
void stasis_signature_add_handle(stasis_signature *signature, const char *group,
                                 const char *object);

/* Starts a record, whose fields follow, each a stasis_signature_begin_field and then its type. */
void stasis_signature_begin_record(stasis_signature *signature);

/* Starts a record's field called `name`, whose type follows. */
void stasis_signature_begin_field(stasis_signature *signature, const char *name);

/* Ends the record that was started last. */
void stasis_signature_end_record(stasis_signature *signature);

/* Ends the parameters, before the result's type. A signature with no result never calls it. */
void stasis_signature_begin_result(stasis_signature *signature);

/* Finishes the signature and writes its hash, as text that ends in a null. Returns the length of
 * its encoding, which the text holds whole when `capacity` is greater than that. */
size_t stasis_signature_finish(stasis_signature *signature,
                               char hash[STASIS_SIGNATURE_HASH_LENGTH + 1]);

#endif
