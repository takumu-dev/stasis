/* A C script built with the C language pack's flags, which is snapshottable as such a script should
 * be: wasm-ld exports its memory, the flags export __stack_pointer, its only mutable global, and
 * nothing in it changes a table. Its code is what the compiler makes of calls through function
 * pointers, the heap, copies, formatting and arithmetic. `starts` returns how many times
 * stasis_initialize has run, as calls.stasis says, which is how many times the constructor has. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void __wasm_call_ctors(void);

static unsigned starts;
static char text[64];

static unsigned add(unsigned a, unsigned b) { return a + b; }

static unsigned multiply(unsigned a, unsigned b) { return a * b; }

static unsigned (*const operations[])(unsigned, unsigned) = {add, multiply};

__attribute__((constructor)) static void count_start(void) {
    char *copy = malloc(sizeof text);
    if (copy != NULL) {
        const int length = snprintf(copy, sizeof text, "%s %d %.3f", "start", 42, 2.5);
        memcpy(text, copy, length > 0 ? (size_t)length : 0);
        memset(copy, 0, sizeof text);
        free(copy);
    }
    starts = operations[strlen(text) % 2](starts, 1) == 0 ? starts + 1 : operations[0](starts, 1);
}

__attribute__((export_name("stasis_initialize"))) void stasis_initialize(void) {
    __wasm_call_ctors();
}

__attribute__((export_name("test:calls/caller@1#starts:5b69886a8b9e31a8"))) unsigned
caller_starts(void) {
    return starts;
}
