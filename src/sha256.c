#include "sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The first 32 bits of the fractional parts of the cube roots of the first 64 primes. */
static const uint32_t round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/* The first 32 bits of the fractional parts of the square roots of the first 8 primes. */
static const uint32_t initial_state[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

static uint32_t rotate_right(uint32_t value, unsigned int bits) {
    return value >> bits | value << (32U - bits);
}

static uint32_t load_big_endian(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 |
           (uint32_t)bytes[3];
}

static void store_big_endian(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

/* Hashes one 64-byte block into the state. */
static void compress(uint32_t state[8], const uint8_t block[64]) {
    uint32_t schedule[64];
    for (size_t i = 0; i < 16; i++) {
        schedule[i] = load_big_endian(block + (4 * i));
    }
    for (size_t i = 16; i < 64; i++) {
        const uint32_t s0 = rotate_right(schedule[i - 15], 7) ^ rotate_right(schedule[i - 15], 18) ^
                            schedule[i - 15] >> 3;
        const uint32_t s1 = rotate_right(schedule[i - 2], 17) ^ rotate_right(schedule[i - 2], 19) ^
                            schedule[i - 2] >> 10;
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];
    for (size_t i = 0; i < 64; i++) {
        const uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const uint32_t choice = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + choice + round_constants[i] + schedule[i];
        const uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void stasis_sha256_init(stasis_sha256 *sha) {
    memcpy(sha->state, initial_state, sizeof sha->state);
    sha->size = 0;
}

void stasis_sha256_update(stasis_sha256 *sha, const void *data, size_t size) {
    if (size == 0) {
        return;
    }
    const uint8_t *bytes = data;
    size_t used = (size_t)(sha->size % 64);
    sha->size += size;

    /* Complete the block an earlier update left unfinished. */
    if (used > 0) {
        const size_t taken = size < 64 - used ? size : 64 - used;
        memcpy(sha->block + used, bytes, taken);
        bytes += taken;
        size -= taken;
        used += taken;
        if (used < 64) {
            return;
        }
        compress(sha->state, sha->block);
    }
    for (; size >= 64; bytes += 64, size -= 64) {
        compress(sha->state, bytes);
    }
    if (size > 0) {
        memcpy(sha->block, bytes, size);
    }
}

void stasis_sha256_final(stasis_sha256 *sha, uint8_t digest[STASIS_SHA256_SIZE]) {
    /* The message is padded with a 1 bit, then zeros up to the last 8 bytes of a block, which hold
     * the message's length in bits. */
    const uint64_t bits = sha->size * 8;
    size_t used = (size_t)(sha->size % 64);
    sha->block[used++] = 0x80;
    if (used > 56) {
        memset(sha->block + used, 0, 64 - used);
        compress(sha->state, sha->block);
        used = 0;
    }
    memset(sha->block + used, 0, 56 - used);
    for (size_t i = 0; i < 8; i++) {
        sha->block[56 + i] = (uint8_t)(bits >> (56 - (8 * i)));
    }
    compress(sha->state, sha->block);

    for (size_t i = 0; i < 8; i++) {
        store_big_endian(digest + (4 * i), sha->state[i]);
    }
}
