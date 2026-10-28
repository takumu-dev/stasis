/* A C script built with the C language pack's flags that takes and returns records through
 * records.stasis, as C structs that the compiler lays out, which for these records is as the
 * canonical ABI lays them out. It provides `echo_outer`, `fetch_outer` and `pass_hits`, as
 * records.stasis says, but `pass_hits` passes each hit to `host.take_hit` in turn, from the fields
 * it reads, and then frees the hits' weapons and the tuple they spilled into, which Stasis
 * allocated with its cabi_realloc. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void __wasm_call_ctors(void);

/* A string as the canonical ABI lays it out in memory: its address, then its size. */
struct string {
    char *data;
    size_t size;
};

/* The records of records.stasis, each field as the C type its type crosses as. */

struct vec2 {
    float x;
    float y;
};

struct hit {
    struct string weapon;
    uint32_t damage;
    struct vec2 at;
};

struct mixed {
    uint8_t a;
    uint64_t b;
    uint16_t c;
    bool d;
    float e;
    int8_t f;
};

struct outer {
    uint8_t tag;
    struct mixed inner;
    int16_t last;
};

/* The arguments of `pass_hits`, as they spill. */
struct hits {
    struct hit a;
    struct hit b;
    struct hit c;
    struct hit d;
};

#define HOST_FUNCTION(name) __attribute__((import_module("test:records/host@1"), import_name(name)))

HOST_FUNCTION("take_hit:7abc176cf089458f")
void host_take_hit(const char *weapon, size_t size, uint32_t damage, float x, float y);
HOST_FUNCTION("give_outer:0d6b48927a4088cf") void host_give_outer(struct outer *result);

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

/* The return area of the entry points that return an `outer`. */
static struct outer returned;

__attribute__((export_name("test:records/user@1#echo_outer:ca2012d2782e70cd"))) struct outer *
echo_outer(uint8_t tag, uint8_t a, uint64_t b, uint16_t c, bool d, float e, int8_t f,
           int16_t last) {
    returned.tag = tag;
    returned.inner.a = a;
    returned.inner.b = b;
    returned.inner.c = c;
    returned.inner.d = d;
    returned.inner.e = e;
    returned.inner.f = f;
    returned.last = last;
    return &returned;
}

__attribute__((export_name("test:records/user@1#fetch_outer:0d6b48927a4088cf"))) struct outer *
fetch_outer(void) {
    struct outer given;
    host_give_outer(&given);
    returned.tag = (uint8_t)(given.tag + 1);
    returned.inner.a = (uint8_t)(given.inner.a + 1);
    returned.inner.b = given.inner.b + 1;
    returned.inner.c = (uint16_t)(given.inner.c + 1);
    returned.inner.d = !given.inner.d;
    returned.inner.e = given.inner.e + 1.0F;
    returned.inner.f = (int8_t)(given.inner.f + 1);
    returned.last = (int16_t)(given.last + 1);
    return &returned;
}

/* Passes a hit to `host.take_hit`, and frees its weapon. */
static void pass(struct hit *hit) {
    host_take_hit(hit->weapon.data, hit->weapon.size, hit->damage, hit->at.x, hit->at.y);
    if (hit->weapon.size > 0) {
        free(hit->weapon.data);
    }
}

__attribute__((export_name("test:records/user@1#pass_hits:d70d8319bc5de9a9"))) void
pass_hits(struct hits *hits) {
    pass(&hits->a);
    pass(&hits->b);
    pass(&hits->c);
    pass(&hits->d);
    free(hits);
}
