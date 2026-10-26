/* The smallest C script that uses the C library: a constructor formats some text with snprintf, and
 * an entry point returns its length. stasis_initialize runs the constructor when an instance
 * starts. */

#include <stdio.h>

void __wasm_call_ctors(void);

static int text_length;

__attribute__((constructor)) static void format_text(void) {
    char text[32];
    text_length = snprintf(text, sizeof text, "%s %d", "tick", 42);
}

__attribute__((export_name("stasis_initialize"))) void stasis_initialize(void) {
    __wasm_call_ctors();
}

__attribute__((export_name("text_length"))) int get_text_length(void) { return text_length; }
