/* SHA-256, as FIPS 180-4 defines it, for messages of whole bytes.
 *
 * It depends on nothing else of Stasis's, so the library and the generator both compile it. */

#ifndef STASIS_SHA256_H
#define STASIS_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* The size of a digest, in bytes. */
enum { STASIS_SHA256_SIZE = 32 };

/* A hash in progress. */
typedef struct stasis_sha256 {
    uint32_t state[8];
    /* The bytes hashed so far. */
    uint64_t size;
    /* The bytes of the block that isn't complete yet: the last size % 64 of them. */
    uint8_t block[64];
} stasis_sha256;

/* Starts a hash. */
void stasis_sha256_init(stasis_sha256 *sha);

/* Hashes the next `size` bytes of the message, which may be null when `size` is 0. */
void stasis_sha256_update(stasis_sha256 *sha, const void *data, size_t size);

/* Finishes the hash and writes its digest. `sha` must be started again before it's used again. */
void stasis_sha256_final(stasis_sha256 *sha, uint8_t digest[STASIS_SHA256_SIZE]);

#endif
