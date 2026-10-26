/* SHA-256 against NIST's test vectors: the examples of FIPS 180-2, and the byte-oriented SHA-256
 * vectors of NIST's Cryptographic Algorithm Validation Program, read from the files NIST publishes
 * in tests/nist. */

#include "sha256.h"

#include "harness.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A way of hashing a whole message. */
typedef void hash_function(const void *message, size_t size, uint8_t digest[STASIS_SHA256_SIZE]);

/* Hashes a whole message with one update. */
static void hash(const void *message, size_t size, uint8_t digest[STASIS_SHA256_SIZE]) {
    stasis_sha256 sha;
    stasis_sha256_init(&sha);
    stasis_sha256_update(&sha, message, size);
    stasis_sha256_final(&sha, digest);
}

/* The sizes of the pieces in which a message is hashed piece by piece, used in turn. Each lands
 * on, before or after a block's end at some point. */
static const size_t piece_sizes[] = {1, 2, 31, 63, 64, 65, 100, 129};

/* Hashes a message piece by piece, with an empty update between pieces. */
static void hash_in_pieces(const void *message, size_t size, uint8_t digest[STASIS_SHA256_SIZE]) {
    const uint8_t *bytes = message;
    stasis_sha256 sha;
    stasis_sha256_init(&sha);
    size_t offset = 0;
    for (size_t i = 0; offset < size; i++) {
        size_t piece = piece_sizes[i % (sizeof piece_sizes / sizeof piece_sizes[0])];
        if (piece > size - offset) {
            piece = size - offset;
        }
        stasis_sha256_update(&sha, bytes + offset, piece);
        stasis_sha256_update(&sha, NULL, 0);
        offset += piece;
    }
    stasis_sha256_final(&sha, digest);
}

/* The value of one hexadecimal digit. */
static uint8_t hex_digit(char digit) {
    const char *digits = "0123456789abcdef";
    const char *found = digit == '\0' ? NULL : strchr(digits, digit);
    ASSERT_TRUE(found != NULL);
    return (uint8_t)(found - digits);
}

/* Decodes `size` bytes from lowercase hexadecimal, which must have exactly that many. */
static void decode_hex(const char *hex, uint8_t *bytes, size_t size) {
    ASSERT_SIZE_EQ(strlen(hex), 2 * size);
    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)(hex_digit(hex[2 * i]) << 4 | hex_digit(hex[(2 * i) + 1]));
    }
}

/* Fails unless a digest is the one given in hexadecimal. */
static void check_digest(const uint8_t digest[STASIS_SHA256_SIZE], const char *expected_hex) {
    uint8_t expected[STASIS_SHA256_SIZE];
    decode_hex(expected_hex, expected, sizeof expected);
    ASSERT_MEM_EQ(digest, expected, sizeof expected);
}

/* Checks the digest of a message given as text. */
static void check_text(const char *message, const char *expected_hex) {
    uint8_t digest[STASIS_SHA256_SIZE];
    hash(message, strlen(message), digest);
    check_digest(digest, expected_hex);
}

TEST(fips_180_one_block_message) {
    check_text("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(fips_180_two_block_message) {
    check_text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

/* A million times "a", hashed in a thousand updates. */
TEST(fips_180_long_message) {
    char thousand[1000];
    memset(thousand, 'a', sizeof thousand);
    stasis_sha256 sha;
    stasis_sha256_init(&sha);
    for (int i = 0; i < 1000; i++) {
        stasis_sha256_update(&sha, thousand, sizeof thousand);
    }
    uint8_t digest[STASIS_SHA256_SIZE];
    stasis_sha256_final(&sha, digest);
    check_digest(digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

/* Reads one of NIST's files whole, as a string the caller frees. */
static char *read_nist_file(const char *name) {
    char path[1024];
    const int length = snprintf(path, sizeof path, "%s/%s", STASIS_NIST_DIR, name);
    ASSERT_TRUE(length > 0 && (size_t)length < sizeof path);
    FILE *file = fopen(path, "rb");
    ASSERT_TRUE(file != NULL);
    size_t size = 0;
    size_t capacity = 1 << 16;
    char *text = malloc(capacity);
    ASSERT_TRUE(text != NULL);
    for (;;) {
        size += fread(text + size, 1, capacity - size - 1, file);
        if (size < capacity - 1) {
            break;
        }
        capacity *= 2;
        char *grown = realloc(text, capacity);
        ASSERT_TRUE(grown != NULL);
        text = grown;
    }
    ASSERT_TRUE(ferror(file) == 0);
    fclose(file);
    text[size] = '\0';
    return text;
}

/* Cuts the next line from a text, dropping its line ending, and moves `cursor` past it. Returns
 * null at the end of the text. */
static char *next_line(char **cursor) {
    char *line = *cursor;
    if (*line == '\0') {
        return NULL;
    }
    char *end = line + strcspn(line, "\n");
    *cursor = *end == '\0' ? end : end + 1;
    *end = '\0';
    if (end > line && end[-1] == '\r') {
        end[-1] = '\0';
    }
    return line;
}

/* Splits a `key = value` line in place, setting `key` and returning the value. Returns null for
 * any other line. */
static const char *split_line(char *line, const char **key) {
    char *separator = strstr(line, " = ");
    if (separator == NULL) {
        return NULL;
    }
    *separator = '\0';
    *key = line;
    return separator + 3;
}

/* Checks every message in one of NIST's message files against its digest, hashing it with
 * `hash_message`. Returns how many messages it checked. */
static size_t check_messages(const char *file_name, hash_function *hash_message) {
    char *text = read_nist_file(file_name);
    size_t checked = 0;
    size_t size = 0;
    uint8_t *message = NULL;
    char *cursor = text;
    for (char *line = next_line(&cursor); line != NULL; line = next_line(&cursor)) {
        const char *key = NULL;
        const char *value = split_line(line, &key);
        if (value == NULL) {
            continue;
        }
        if (strcmp(key, "Len") == 0) {
            const unsigned long bits = strtoul(value, NULL, 10);
            ASSERT_UINT_EQ(bits % 8, 0);
            size = bits / 8;
        } else if (strcmp(key, "Msg") == 0) {
            /* An empty message is written as one zero byte. */
            free(message);
            message = malloc(size + 1);
            ASSERT_TRUE(message != NULL);
            decode_hex(size == 0 ? "" : value, message, size);
        } else if (strcmp(key, "MD") == 0) {
            ASSERT_TRUE(message != NULL);
            uint8_t digest[STASIS_SHA256_SIZE];
            hash_message(message, size, digest);
            check_digest(digest, value);
            checked++;
        }
    }
    free(message);
    free(text);
    return checked;
}

TEST(nist_short_messages) { ASSERT_SIZE_EQ(check_messages("SHA256ShortMsg.rsp", hash), 65); }

TEST(nist_long_messages) { ASSERT_SIZE_EQ(check_messages("SHA256LongMsg.rsp", hash), 64); }

TEST(nist_long_messages_in_pieces) {
    ASSERT_SIZE_EQ(check_messages("SHA256LongMsg.rsp", hash_in_pieces), 64);
}

/* NIST's Monte Carlo test: from a seed, each of 100 checkpoints follows from the one before by
 * 1,000 hashes, each of the three digests before it. */
TEST(nist_monte_carlo) {
    char *text = read_nist_file("SHA256Monte.rsp");
    uint8_t seed[STASIS_SHA256_SIZE];
    bool seeded = false;
    size_t checked = 0;
    char *cursor = text;
    for (char *line = next_line(&cursor); line != NULL; line = next_line(&cursor)) {
        const char *key = NULL;
        const char *value = split_line(line, &key);
        if (value == NULL) {
            continue;
        }
        if (strcmp(key, "Seed") == 0) {
            decode_hex(value, seed, sizeof seed);
            seeded = true;
        } else if (strcmp(key, "MD") == 0) {
            ASSERT_TRUE(seeded);
            /* The three digests before the next, oldest first, hashed as one message. */
            uint8_t digests[3][STASIS_SHA256_SIZE];
            for (size_t i = 0; i < 3; i++) {
                memcpy(digests[i], seed, sizeof seed);
            }
            for (int i = 0; i < 1000; i++) {
                uint8_t digest[STASIS_SHA256_SIZE];
                hash(digests, sizeof digests, digest);
                memmove(digests[0], digests[1], 2 * sizeof digests[0]);
                memcpy(digests[2], digest, sizeof digest);
            }
            memcpy(seed, digests[2], sizeof seed);
            check_digest(seed, value);
            checked++;
        }
    }
    free(text);
    ASSERT_SIZE_EQ(checked, 100);
}

TEST_LIST(TEST_CASE(fips_180_one_block_message), TEST_CASE(fips_180_two_block_message),
          TEST_CASE(fips_180_long_message), TEST_CASE(nist_short_messages),
          TEST_CASE(nist_long_messages), TEST_CASE(nist_long_messages_in_pieces),
          TEST_CASE(nist_monte_carlo));
