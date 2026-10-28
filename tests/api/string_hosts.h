/* The host functions of strings.stasis, which every program that calls the strings script links:
 * each records what it's given, copying each string, which points into the script's memory only
 * until it returns, and returns what the case has set. */

#ifndef STASIS_TESTS_API_STRING_HOSTS_H
#define STASIS_TESTS_API_STRING_HOSTS_H

#include <stasis.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most strings a host function takes. */
enum { MOST_TAKEN = 9 };

/* What the last host function to take strings was given. */
struct taken {
    /* The number of calls of host functions that take strings since forget_taken. */
    unsigned calls;

    /* Each string, in order: a copy of its bytes, which is null when it's empty, its size, and
     * where its data pointed, which is never dereferenced once the host function has returned. */
    struct {
        char *bytes;
        size_t size;
        const char *data;
    } strings[MOST_TAKEN];

    size_t count;
    /* The scalars `take_mixed` and `take_nine` were given. */
    uint8_t before;
    uint64_t middle;
    float after;
    uint8_t last;
};

extern struct taken taken;

/* The string `give` and `swap` return. */
extern stasis_string reply;

/* What `give` and `swap` do before they return, if the case has set it. */
extern void (*on_give)(stasis_instance *instance);

/* Frees the copies, and forgets every call. */
void forget_taken(void);

/* Whether the string at `index` of those taken is `expected`, a NUL-terminated string. */
bool taken_is(size_t index, const char *expected);

#endif
