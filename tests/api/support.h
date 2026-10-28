/* What the tests of the C API share: the scripts the build makes from source, read as a host reads
 * them, the steps that come before what a test checks, and threads. Each step fails the case when
 * it fails. */

#ifndef STASIS_TESTS_API_SUPPORT_H
#define STASIS_TESTS_API_SUPPORT_H

#include <stasis.h>

#include <stddef.h>
#include <stdint.h>

/* A module's bytes, as a file holds them. */
struct module_bytes {
    uint8_t *bytes;
    size_t size;
};

/* Reads the script <name>.wasm that the build made. */
struct module_bytes read_module(const char *name);

/* Frees what read_module read. */
void free_module(struct module_bytes module);

/* Creates a library with the default settings. */
stasis_library *create_library(void);

/* Loads the script <name>.wasm into `library` against `interface_data`, which must succeed with an
 * empty message. */
stasis_script *load_script(stasis_library *library, const stasis_interface *interface_data,
                           const char *name);

/* Creates an instance of `script` with no user data, and starts it. */
stasis_instance *start_instance(stasis_script *script);

/* Destroys an instance, its script and their library, each of which must succeed. */
void destroy_all(stasis_instance *instance, stasis_script *script, stasis_library *library);

/* Runs `body` with `argument` on a thread of its own, whose stack holds `stack_size` bytes, and
 * waits for it to end. On Linux, the thread has no guard page below its stack, which WAMR would
 * count as stack it keeps clear. */
void run_on_thread(void (*body)(void *argument), void *argument, size_t stack_size);

/* The lowest address of the calling thread's stack that it can use. */
uintptr_t stack_end(void);

#endif
