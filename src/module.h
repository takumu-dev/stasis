/* A WebAssembly module, as the core reads it for itself, without WAMR: its identity, and the
 * types, imports, functions and exports that the load check looks at.
 *
 * The module's bytes are untrusted, so the reader checks every length and count against the bytes
 * that hold it. It isn't a validator, though: it reads only what the load check needs, and WAMR's
 * loader has the first say on whether a module is valid. */

#ifndef STASIS_MODULE_H
#define STASIS_MODULE_H

#include <stasis.h>

#include <stdint.h>

/* The kinds of import and export. */
enum {
    MODULE_FUNCTION = 0,
    MODULE_TABLE = 1,
    MODULE_MEMORY = 2,
    MODULE_GLOBAL = 3,
};

/* The core value types, as the binary format encodes them. */
enum {
    MODULE_I32 = 0x7f,
    MODULE_I64 = 0x7e,
    MODULE_F32 = 0x7d,
    MODULE_F64 = 0x7c,
};

/* A function type: its parameters' and results' value types, each one byte. */
struct module_function_type {
    const uint8_t *parameters;
    uint32_t parameter_count;
    const uint8_t *results;
    uint32_t result_count;
};

struct module_import {
    stasis_string module;
    stasis_string field;
    uint8_t kind;
    /* A function's type index, and 0 for any other kind. */
    uint32_t type;
};

struct module_export {
    stasis_string name;
    uint8_t kind;
    /* The index of what's exported, in the index space of its kind. */
    uint32_t index;
};

struct module {
    /* The SHA-256 of the non-custom sections, in order. */
    uint8_t identity[STASIS_IDENTITY_SIZE];
    struct module_function_type *types;
    uint32_t type_count;
    struct module_import *imports;
    uint32_t import_count;
    /* The number of imports that are functions, which come first in the function index space. */
    uint32_t function_import_count;
    /* The type index of each function the module defines, after those it imports. */
    uint32_t *functions;
    uint32_t function_count;
    struct module_export *exports;
    uint32_t export_count;
};

/* How reading a module ended. */
enum module_read {
    MODULE_READ_OK,
    /* The bytes aren't a module the reader can read. */
    MODULE_READ_MALFORMED,
    MODULE_READ_OUT_OF_MEMORY,
};

/* Reads the module in `bytes`. Its names point into the bytes, which must outlive it. Whatever the
 * result, `module` must be freed with stasis_module_free. */
enum module_read stasis_module_read(struct module *module, const uint8_t *bytes, size_t size);

/* Frees what reading a module allocated. */
void stasis_module_free(struct module *module);

/* The type of the function at `index` in the function index space, or null if there's no such
 * function or type. */
const struct module_function_type *stasis_module_function_type(const struct module *module,
                                                               uint32_t index);

#endif
