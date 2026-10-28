/* A C script built with the C language pack's flags that takes and returns strings through
 * strings.stasis, as its bindings will: its cabi_realloc allocates with the C library's realloc,
 * each entry point frees the strings Stasis passed it once it's done with them, and each
 * cabi_post_* frees the string its entry point returned. It provides `pass`, `fetch`, `relay` and
 * `echo`, as strings.stasis says. */

#include <stddef.h>
#include <stdlib.h>

void __wasm_call_ctors(void);

/* A string as the canonical ABI lays it out in memory: its address, then its size. */
struct string {
    char *data;
    size_t size;
};

#define HOST_FUNCTION(name) __attribute__((import_module("test:strings/host@1"), import_name(name)))

HOST_FUNCTION("take:76a67b5b4fa368b5") void host_take(const char *data, size_t size);
HOST_FUNCTION("give:ae6637fc59233091") void host_give(struct string *result);
HOST_FUNCTION("swap:150b24eac1c4a9c1")
void host_swap(const char *data, size_t size, struct string *result);

__attribute__((export_name("stasis_initialize"))) void stasis_initialize(void) {
    __wasm_call_ctors();
}

/* As wit-bindgen's does, it answers an allocation of 0 bytes with an address that needn't be
 * freed. */
__attribute__((export_name("cabi_realloc"))) void *cabi_realloc(void *old, size_t old_size,
                                                                size_t alignment, size_t size) {
    (void)old_size;
    if (size == 0) {
        return (void *)alignment;
    }
    return realloc(old, size);
}

/* Frees a string that Stasis allocated with cabi_realloc. */
static void free_string(char *data, size_t size) {
    if (size > 0) {
        free(data);
    }
}

/* The return area of the entry points that return a string. */
static struct string returned;

__attribute__((export_name("test:strings/user@1#pass:76a67b5b4fa368b5"))) void pass(char *data,
                                                                                    size_t size) {
    host_take(data, size);
    free_string(data, size);
}

__attribute__((export_name("test:strings/user@1#fetch:ae6637fc59233091"))) struct string *
fetch(void) {
    host_give(&returned);
    return &returned;
}

__attribute__((export_name("test:strings/user@1#relay:150b24eac1c4a9c1"))) struct string *
relay(char *data, size_t size) {
    host_swap(data, size, &returned);
    free_string(data, size);
    return &returned;
}

__attribute__((export_name("test:strings/user@1#echo:150b24eac1c4a9c1"))) struct string *
echo(char *data, size_t size) {
    returned.data = data;
    returned.size = size;
    return &returned;
}

/* Each entry point's cabi_post_* frees the string it returned. */

__attribute__((export_name("cabi_post_test:strings/user@1#fetch:ae6637fc59233091"))) void
fetch_post(struct string *result) {
    free_string(result->data, result->size);
}

__attribute__((export_name("cabi_post_test:strings/user@1#relay:150b24eac1c4a9c1"))) void
relay_post(struct string *result) {
    free_string(result->data, result->size);
}

__attribute__((export_name("cabi_post_test:strings/user@1#echo:150b24eac1c4a9c1"))) void
echo_post(struct string *result) {
    free_string(result->data, result->size);
}
