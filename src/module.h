/* A WebAssembly module, as the core reads it for itself, without WAMR: its identity, the types,
 * imports, functions, tables, memories, globals, exports, start section, segments and instructions
 * that the load check looks at, and where its code is, which trap reports look at.
 *
 * The module's bytes are untrusted, so the reader checks every length and count against the bytes
 * that hold it. It isn't a validator, though: it reads only what the load check needs, and WAMR's
 * loader has the first say on whether a module is valid. */

#ifndef STASIS_MODULE_H
#define STASIS_MODULE_H

#include <stasis.h>

#include <stdbool.h>
#include <stdint.h>

/* The kinds of import and export. */
enum {
    MODULE_FUNCTION = 0,
    MODULE_TABLE = 1,
    MODULE_MEMORY = 2,
    MODULE_GLOBAL = 3,
};

/* The value types, as the binary format encodes them. */
enum {
    MODULE_I32 = 0x7f,
    MODULE_I64 = 0x7e,
    MODULE_F32 = 0x7d,
    MODULE_F64 = 0x7c,
    MODULE_FUNCREF = 0x70,
    MODULE_EXTERNREF = 0x6f,
};

/* The size of a page of linear memory, in bytes. */
enum { MODULE_PAGE_SIZE = 65536 };

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

/* A table the module defines: the number of elements it starts with. */
struct module_table {
    uint32_t minimum;
};

/* A memory the module defines: the number of pages it starts with, whether it's shared, and
 * whether the module exports it. */
struct module_memory {
    uint32_t minimum;
    bool shared;
    bool exported;
};

/* A global the module defines: its value type, whether it's mutable, and whether the module
 * exports it. */
struct module_global {
    uint8_t type;
    bool is_mutable;
    bool exported;
};

/* The instructions that change a table or drop a segment, which a snapshot holds the effects of
 * neither. */
enum module_change {
    MODULE_TABLE_SET,
    MODULE_TABLE_GROW,
    MODULE_TABLE_FILL,
    MODULE_TABLE_COPY,
    MODULE_TABLE_INIT,
    MODULE_ELEM_DROP,
    MODULE_DATA_DROP,
    /* The number of them, which is none of them. */
    MODULE_CHANGE_COUNT,
};

/* Whether a module's code contains one of those instructions, and if so, the index of the first
 * function that does, in the function index space. */
struct module_first_use {
    bool used;
    uint32_t function;
};

/* A segment of data or elements, which a module copies into a memory or a table: an active
 * segment when an instance is created, and a passive or declared one only if an instruction says
 * so. */
struct module_segment {
    bool active;
    /* For an active segment, the index of the memory or the table it's copied into. */
    uint32_t index;
    /* Whether an active segment's offset is a constant, an `i32.const` alone, and if so, the
     * offset. Any other offset reads an imported global. */
    bool constant_offset;
    uint32_t offset;
    /* The number of bytes or elements it holds. */
    uint32_t size;
};

struct module_export {
    stasis_string name;
    uint8_t kind;
    /* The index of what's exported, in the index space of its kind. */
    uint32_t index;
};

/* Where a function the module defines has its code: the offsets in the module's bytes of its first
 * instruction, after its locals, and of the end of its body. */
struct module_body {
    uint32_t start;
    uint32_t end;
};

/* Where the module's code is, down to where each instruction starts, which tells the instruction
 * that an offset a backend gives for a frame falls in. */
struct module_code {
    /* The number of functions the module imports, which come before those it defines. */
    uint32_t function_import_count;
    /* The body of each function the module defines. */
    struct module_body *bodies;
    uint32_t body_count;
    /* A bit for each byte of the module up to the end of its code, set where an instruction
     * starts: bit `offset % 8` of byte `offset / 8`. */
    uint8_t *instruction_starts;
};

struct module {
    /* The module's bytes, which the offsets it holds count from. */
    const uint8_t *bytes;
    /* Whether the reader notes where the code is, in `code`, which is empty otherwise. */
    bool maps_code;
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
    /* The number of imports that are tables, which come first in the table index space. */
    uint32_t table_import_count;
    /* The tables the module defines, after those it imports. */
    struct module_table *tables;
    uint32_t table_count;
    /* The number of imports that are memories, which come first in the memory index space. */
    uint32_t memory_import_count;
    /* The memories the module defines, after those it imports. */
    struct module_memory *memories;
    uint32_t memory_count;
    /* The number of imports that are globals, which come first in the global index space. */
    uint32_t global_import_count;
    /* The globals the module defines, after those it imports. */
    struct module_global *globals;
    uint32_t global_count;
    struct module_export *exports;
    uint32_t export_count;
    /* Whether the module has a start section. */
    bool has_start;
    struct module_segment *element_segments;
    uint32_t element_segment_count;
    struct module_segment *data_segments;
    uint32_t data_segment_count;
    /* Where the code first uses each instruction that changes a table or drops a segment, by its
     * enum module_change. */
    struct module_first_use first_uses[MODULE_CHANGE_COUNT];
    struct module_code code;
};

/* How reading a module ended. */
enum module_read {
    MODULE_READ_OK,
    /* The bytes aren't a module the reader can read. */
    MODULE_READ_MALFORMED,
    MODULE_READ_OUT_OF_MEMORY,
};

/* Reads the module in `bytes`. Its names point into the bytes, which must outlive it. Whatever the
 * result, `module` must be freed with stasis_module_free. Where the code is, which only trap
 * reports need, and only with a backend whose frames give offsets in the module, is noted only if
 * `maps_code` is set. */
enum module_read stasis_module_read(struct module *module, const uint8_t *bytes, size_t size,
                                    bool maps_code);

/* Frees what reading a module allocated. */
void stasis_module_free(struct module *module);

/* Frees what reading a module's code allocated, which stasis_module_free does too. */
void stasis_module_code_free(struct module_code *code);

/* The offset of the instruction that holds the byte before `offset`, among the instructions of the
 * function at `function` in the function index space. That's the instruction a frame of the
 * function was at, when a backend gives the offset just past it. It's STASIS_NO_OFFSET when none of
 * the function's instructions holds that byte, and for a function the module imports. */
uint32_t stasis_module_instruction_before(const struct module_code *code, uint32_t function,
                                          uint32_t offset);

/* How a message spells a value type, or "?" for a byte that isn't one. */
const char *stasis_module_type_name(uint8_t type);

/* The table at `index` in the table index space, or null if the module imports it or has no such
 * table. */
const struct module_table *stasis_module_table(const struct module *module, uint32_t index);

/* The memory at `index` in the memory index space, or null if the module imports it or has no
 * such memory. */
const struct module_memory *stasis_module_memory(const struct module *module, uint32_t index);

/* The type of the function at `index` in the function index space, or null if there's no such
 * function or type. */
const struct module_function_type *stasis_module_function_type(const struct module *module,
                                                               uint32_t index);

#endif
