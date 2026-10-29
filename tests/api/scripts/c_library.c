/* A C script built with the C language pack's flags, each of whose entry points uses a part of the
 * C library that reaches the system interface: stdio, assert, time, arc4random, getenv, exit and
 * fopen. It provides every entry point of c_library.stasis. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void __wasm_call_ctors(void);

#define ENTRY_POINT(name) __attribute__((export_name("test:c_library/user@1#" name)))

__attribute__((export_name("stasis_initialize"))) void stasis_initialize(void) {
    __wasm_call_ctors();
}

ENTRY_POINT("print_line:2e38e77b22c314a4") void print_line(void) {
    printf("hello, ");
    printf("world\n");
}

ENTRY_POINT("print_partial:2e38e77b22c314a4") void print_partial(void) { printf("partial"); }

ENTRY_POINT("print_warning:2e38e77b22c314a4") void print_warning(void) {
    fprintf(stderr, "low health");
}

/* What the script believes its health is, which it can't know until it runs. */
static volatile int health;

ENTRY_POINT("fail_assert:2e38e77b22c314a4") void fail_assert(void) { assert(health > 0); }

ENTRY_POINT("read_time:41a38dbf6c4bf90d") int64_t read_time(void) { return time(NULL); }

ENTRY_POINT("draw_random:5b69886a8b9e31a8") uint32_t draw_random(void) { return arc4random(); }

ENTRY_POINT("has_home:9db791dcb68a6c42") bool has_home(void) { return getenv("HOME") != NULL; }

ENTRY_POINT("exit_zero:2e38e77b22c314a4") void exit_zero(void) { exit(0); }

ENTRY_POINT("open_save:9db791dcb68a6c42") bool open_save(void) {
    FILE *save = fopen("save.txt", "r");
    if (save == NULL) {
        return false;
    }
    (void)fclose(save);
    return true;
}
