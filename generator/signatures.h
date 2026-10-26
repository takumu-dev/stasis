/* The signatures of an interface's host functions, methods and entry points: the encoding of each,
 * and its hash, which name a script's imports and entry points. */

#ifndef STASIS_GENERATOR_SIGNATURES_H
#define STASIS_GENERATOR_SIGNATURES_H

#include "arena.h"
#include "interface.h"
#include "signature.h"

/* A function's signature. */
struct signature {
    const char *encoding;
    char hash[STASIS_SIGNATURE_HASH_LENGTH + 1];
};

/* Encodes the signature of `function`, which must be in an interface read with no problems. */
void encode_signature(const struct function *function, struct arena *arena,
                      struct signature *signature);

#endif
