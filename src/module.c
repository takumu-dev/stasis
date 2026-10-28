#include "module.h"

#include <stasis.h>

#include "allocator.h"
#include "sha256.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The sections the reader reads. Every other is skipped, apart from going into the identity. */
enum {
    SECTION_CUSTOM = 0,
    SECTION_TYPE = 1,
    SECTION_IMPORT = 2,
    SECTION_FUNCTION = 3,
    SECTION_TABLE = 4,
    SECTION_MEMORY = 5,
    SECTION_GLOBAL = 6,
    SECTION_EXPORT = 7,
    SECTION_START = 8,
    SECTION_ELEMENT = 9,
    SECTION_CODE = 10,
    SECTION_DATA = 11,
};

/* The form that starts a function type. */
enum { FUNCTION_TYPE = 0x60 };

/* The opcodes of the instructions WAMR accepts, as Stasis builds it, with the first and last of
 * each range whose instructions take the same immediates. Those after OPCODE_PREFIX are below. */
enum {
    OPCODE_UNREACHABLE = 0x00,
    OPCODE_NOP = 0x01,
    OPCODE_BLOCK = 0x02,
    OPCODE_LOOP = 0x03,
    OPCODE_IF = 0x04,
    OPCODE_ELSE = 0x05,
    OPCODE_END = 0x0b,
    OPCODE_BR = 0x0c,
    OPCODE_BR_IF = 0x0d,
    OPCODE_BR_TABLE = 0x0e,
    OPCODE_RETURN = 0x0f,
    OPCODE_CALL = 0x10,
    OPCODE_CALL_INDIRECT = 0x11,
    OPCODE_DROP = 0x1a,
    OPCODE_SELECT = 0x1b,
    OPCODE_SELECT_TYPED = 0x1c,
    /* local.get, local.set, local.tee, global.get and global.set. */
    OPCODE_FIRST_VARIABLE = 0x20,
    OPCODE_LAST_VARIABLE = 0x24,
    OPCODE_TABLE_GET = 0x25,
    OPCODE_TABLE_SET = 0x26,
    OPCODE_FIRST_MEMORY_ACCESS = 0x28,
    OPCODE_LAST_MEMORY_ACCESS = 0x3e,
    OPCODE_MEMORY_SIZE = 0x3f,
    OPCODE_MEMORY_GROW = 0x40,
    OPCODE_I32_CONST = 0x41,
    OPCODE_I64_CONST = 0x42,
    OPCODE_F32_CONST = 0x43,
    OPCODE_F64_CONST = 0x44,
    OPCODE_FIRST_NUMERIC = 0x45,
    OPCODE_LAST_NUMERIC = 0xc4,
    OPCODE_REF_NULL = 0xd0,
    OPCODE_REF_IS_NULL = 0xd1,
    OPCODE_REF_FUNC = 0xd2,
    OPCODE_PREFIX = 0xfc,
};

/* The opcodes that follow OPCODE_PREFIX: the conversions that saturate, which take no immediates,
 * up to OPCODE_LAST_SATURATING, and then bulk memory and the table instructions. */
enum {
    OPCODE_LAST_SATURATING = 7,
    OPCODE_MEMORY_INIT = 8,
    OPCODE_DATA_DROP = 9,
    OPCODE_MEMORY_COPY = 10,
    OPCODE_MEMORY_FILL = 11,
    OPCODE_TABLE_INIT = 12,
    OPCODE_ELEM_DROP = 13,
    OPCODE_TABLE_COPY = 14,
    OPCODE_TABLE_GROW = 15,
    OPCODE_TABLE_SIZE = 16,
    OPCODE_TABLE_FILL = 17,
};

/* The flags that start an element segment: whether it's passive or declared rather than active,
 * whether an active one names its table, and whether its elements are expressions rather than
 * function indices. */
#define ELEMENT_NOT_ACTIVE 1U
#define ELEMENT_TABLE_INDEX 2U
#define ELEMENT_EXPRESSIONS 4U

/* The flags that start a data segment, other than 0 for one that's active in memory 0: passive,
 * or active in the memory it names. */
enum {
    DATA_PASSIVE = 1,
    DATA_MEMORY_INDEX = 2,
};

/* A reader of the bytes from `at` to `end`, which has failed once anything it read was malformed
 * or ran past the end. Once it has failed, it reads only zeros. */
struct reader {
    const uint8_t *at;
    const uint8_t *end;
    bool failed;
};

/* The number of bytes left to read. */
static size_t remaining(const struct reader *reader) { return (size_t)(reader->end - reader->at); }

/* Reads one byte. */
static uint8_t read_byte(struct reader *reader) {
    if (reader->failed || reader->at == reader->end) {
        reader->failed = true;
        return 0;
    }
    return *reader->at++;
}

/* Reads an unsigned LEB128 number of at most 32 bits, in at most 5 bytes. */
static uint32_t read_u32(struct reader *reader) {
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        const uint8_t byte = read_byte(reader);
        if (shift == 28 && (byte & 0x70) != 0) {
            reader->failed = true;
        }
        value |= (uint32_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            return reader->failed ? 0 : value;
        }
    }
    reader->failed = true;
    return 0;
}

/* Reads a signed LEB128 number of at most `bits` bits, returning its two's complement bits. */
static uint64_t read_signed(struct reader *reader, unsigned bits) {
    uint64_t value = 0;
    unsigned shift = 0;
    for (unsigned i = 0; i < (bits + 6) / 7; i++) {
        const uint8_t byte = read_byte(reader);
        value |= (uint64_t)(byte & 0x7f) << shift;
        shift += 7;
        if ((byte & 0x80) == 0) {
            if (shift < 64 && (byte & 0x40) != 0) {
                value |= ~(uint64_t)0 << shift;
            }
            return reader->failed ? 0 : value;
        }
    }
    reader->failed = true;
    return 0;
}

/* Reads `size` bytes, returning where they start, or null if fewer remain. */
static const uint8_t *read_bytes(struct reader *reader, size_t size) {
    if (reader->failed || remaining(reader) < size) {
        reader->failed = true;
        return NULL;
    }
    const uint8_t *bytes = reader->at;
    reader->at += size;
    return bytes;
}

/* Reads a name: its length, then its bytes. */
static stasis_string read_name(struct reader *reader) {
    stasis_string name;
    name.size = read_u32(reader);
    name.data = (const char *)read_bytes(reader, name.size);
    if (reader->failed) {
        name.data = NULL;
        name.size = 0;
    }
    return name;
}

/* Reads a vector's count, of items that each take at least `least` bytes, which bounds it by the
 * bytes that remain. */
static uint32_t read_count(struct reader *reader, size_t least) {
    const uint32_t count = read_u32(reader);
    if (count > remaining(reader) / least) {
        reader->failed = true;
        return 0;
    }
    return count;
}

/* Reads a table's or a memory's limits: a flag, the minimum, which it returns, and the maximum if
 * the flag says there is one. Unless `shared` is null, it's set to whether the flag says it's
 * shared. */
static uint32_t read_limits(struct reader *reader, bool *shared) {
    const uint8_t flags = read_byte(reader);
    if (flags > 3) {
        reader->failed = true;
    }
    const uint32_t minimum = read_u32(reader);
    if ((flags & 1) != 0) {
        (void)read_u32(reader);
    }
    if (shared != NULL) {
        *shared = (flags & 2) != 0;
    }
    return minimum;
}

/* Reads `count` unsigned LEB128 numbers of at most 32 bits, such as indices, which the reader
 * doesn't keep. */
static void read_u32s(struct reader *reader, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        (void)read_u32(reader);
    }
}

/* What read_immediates returns for an instruction that changes no table and drops no segment. */
#define NO_CHANGE MODULE_CHANGE_COUNT

/* Reads the opcode that follows OPCODE_PREFIX, and the immediates of its instruction, as
 * read_immediates does. */
static enum module_change read_prefixed(struct reader *reader) {
    const uint32_t opcode = read_u32(reader);
    if (opcode <= OPCODE_LAST_SATURATING) {
        return NO_CHANGE;
    }
    switch (opcode) {
    case OPCODE_MEMORY_INIT:
    case OPCODE_MEMORY_COPY:
        read_u32s(reader, 2);
        return NO_CHANGE;
    case OPCODE_MEMORY_FILL:
    case OPCODE_TABLE_SIZE:
        read_u32s(reader, 1);
        return NO_CHANGE;
    case OPCODE_DATA_DROP:
        read_u32s(reader, 1);
        return MODULE_DATA_DROP;
    case OPCODE_TABLE_INIT:
        read_u32s(reader, 2);
        return MODULE_TABLE_INIT;
    case OPCODE_ELEM_DROP:
        read_u32s(reader, 1);
        return MODULE_ELEM_DROP;
    case OPCODE_TABLE_COPY:
        read_u32s(reader, 2);
        return MODULE_TABLE_COPY;
    case OPCODE_TABLE_GROW:
        read_u32s(reader, 1);
        return MODULE_TABLE_GROW;
    case OPCODE_TABLE_FILL:
        read_u32s(reader, 1);
        return MODULE_TABLE_FILL;
    default:
        reader->failed = true;
        return NO_CHANGE;
    }
}

/* Reads the immediates of the instruction whose opcode was just read. Returns which instruction
 * that changes a table or drops a segment it is, or NO_CHANGE for any other. An opcode
 * the reader doesn't know fails. */
static enum module_change read_immediates(struct reader *reader, uint8_t opcode) {
    if (opcode >= OPCODE_FIRST_NUMERIC && opcode <= OPCODE_LAST_NUMERIC) {
        return NO_CHANGE;
    }
    if (opcode >= OPCODE_FIRST_MEMORY_ACCESS && opcode <= OPCODE_LAST_MEMORY_ACCESS) {
        /* The alignment and the offset. */
        read_u32s(reader, 2);
        return NO_CHANGE;
    }
    if (opcode >= OPCODE_FIRST_VARIABLE && opcode <= OPCODE_LAST_VARIABLE) {
        read_u32s(reader, 1);
        return NO_CHANGE;
    }
    switch (opcode) {
    case OPCODE_UNREACHABLE:
    case OPCODE_NOP:
    case OPCODE_ELSE:
    case OPCODE_END:
    case OPCODE_RETURN:
    case OPCODE_DROP:
    case OPCODE_SELECT:
    case OPCODE_REF_IS_NULL:
        return NO_CHANGE;
    case OPCODE_BLOCK:
    case OPCODE_LOOP:
    case OPCODE_IF:
        /* The block type: empty, a value type, or a type index, as a signed 33-bit number. */
        (void)read_signed(reader, 33);
        return NO_CHANGE;
    case OPCODE_BR:
    case OPCODE_BR_IF:
    case OPCODE_CALL:
    case OPCODE_TABLE_GET:
    case OPCODE_MEMORY_SIZE:
    case OPCODE_MEMORY_GROW:
    case OPCODE_REF_FUNC:
        read_u32s(reader, 1);
        return NO_CHANGE;
    case OPCODE_BR_TABLE:
        /* The labels, and then the default label. */
        read_u32s(reader, read_count(reader, 1) + 1U);
        return NO_CHANGE;
    case OPCODE_CALL_INDIRECT:
        /* The type and the table. */
        read_u32s(reader, 2);
        return NO_CHANGE;
    case OPCODE_SELECT_TYPED:
        (void)read_bytes(reader, read_count(reader, 1));
        return NO_CHANGE;
    case OPCODE_TABLE_SET:
        read_u32s(reader, 1);
        return MODULE_TABLE_SET;
    case OPCODE_I32_CONST:
        (void)read_signed(reader, 32);
        return NO_CHANGE;
    case OPCODE_I64_CONST:
        (void)read_signed(reader, 64);
        return NO_CHANGE;
    case OPCODE_F32_CONST:
        (void)read_bytes(reader, 4);
        return NO_CHANGE;
    case OPCODE_F64_CONST:
        (void)read_bytes(reader, 8);
        return NO_CHANGE;
    case OPCODE_REF_NULL:
        (void)read_byte(reader);
        return NO_CHANGE;
    case OPCODE_PREFIX:
        return read_prefixed(reader);
    default:
        reader->failed = true;
        return NO_CHANGE;
    }
}

/* Reads a constant expression, up to and including its `end`. Returns whether it's an `i32.const`
 * alone, and if so, sets `value` to the constant's bits. */
static bool read_constant_expression(struct reader *reader, uint32_t *value) {
    *value = 0;
    bool alone = false;
    for (uint32_t count = 0;; count++) {
        const uint8_t opcode = read_byte(reader);
        if (reader->failed) {
            return false;
        }
        if (opcode == OPCODE_END) {
            return alone;
        }
        if (count == 0 && opcode == OPCODE_I32_CONST) {
            alone = true;
            *value = (uint32_t)read_signed(reader, 32);
        } else {
            alone = false;
            (void)read_immediates(reader, opcode);
        }
    }
}

/* Reads the type section: each function type's parameters and results. */
static enum module_read read_types(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 3);
    module->types = stasis_allocate_zeroed(count, sizeof *module->types);
    if (module->types == NULL) {
        return MODULE_READ_OUT_OF_MEMORY;
    }
    module->type_count = count;
    for (uint32_t i = 0; i < count; i++) {
        struct module_function_type *type = &module->types[i];
        if (read_byte(reader) != FUNCTION_TYPE) {
            reader->failed = true;
        }
        type->parameter_count = read_count(reader, 1);
        type->parameters = read_bytes(reader, type->parameter_count);
        type->result_count = read_count(reader, 1);
        type->results = read_bytes(reader, type->result_count);
    }
    return MODULE_READ_OK;
}

/* Reads the import section: each import's names and kind, and a function's type, skipping what
 * other kinds of import describe. */
static enum module_read read_imports(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 4);
    module->imports = stasis_allocate_zeroed(count, sizeof *module->imports);
    if (module->imports == NULL) {
        return MODULE_READ_OUT_OF_MEMORY;
    }
    module->import_count = count;
    for (uint32_t i = 0; i < count; i++) {
        struct module_import *import = &module->imports[i];
        import->module = read_name(reader);
        import->field = read_name(reader);
        import->kind = read_byte(reader);
        switch (import->kind) {
        case MODULE_FUNCTION:
            import->type = read_u32(reader);
            module->function_import_count++;
            break;
        case MODULE_TABLE:
            (void)read_byte(reader);
            (void)read_limits(reader, NULL);
            module->table_import_count++;
            break;
        case MODULE_MEMORY:
            (void)read_limits(reader, NULL);
            module->memory_import_count++;
            break;
        case MODULE_GLOBAL:
            (void)read_byte(reader);
            (void)read_byte(reader);
            module->global_import_count++;
            break;
        default:
            reader->failed = true;
            break;
        }
    }
    return MODULE_READ_OK;
}

/* Reads the function section: the type of each function the module defines. */
static enum module_read read_functions(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 1);
    module->functions = stasis_allocate_zeroed(count, sizeof *module->functions);
    if (module->functions == NULL) {
        return MODULE_READ_OUT_OF_MEMORY;
    }
    module->function_count = count;
    for (uint32_t i = 0; i < count; i++) {
        module->functions[i] = read_u32(reader);
    }
    return MODULE_READ_OK;
}

/* Reads the table section: each table's minimum. */
static enum module_read read_tables(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 3);
    module->tables = stasis_allocate_zeroed(count, sizeof *module->tables);
    if (module->tables == NULL) {
        return MODULE_READ_OUT_OF_MEMORY;
    }
    module->table_count = count;
    for (uint32_t i = 0; i < count; i++) {
        (void)read_byte(reader);
        module->tables[i].minimum = read_limits(reader, NULL);
    }
    return MODULE_READ_OK;
}

/* Reads the memory section: each memory's minimum and whether it's shared. */
static enum module_read read_memories(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 2);
    module->memories = stasis_allocate_zeroed(count, sizeof *module->memories);
    if (module->memories == NULL) {
        return MODULE_READ_OUT_OF_MEMORY;
    }
    module->memory_count = count;
    for (uint32_t i = 0; i < count; i++) {
        module->memories[i].minimum = read_limits(reader, &module->memories[i].shared);
    }
    return MODULE_READ_OK;
}

/* Reads the global section: each global's type and mutability, skipping its initial value. */
static enum module_read read_globals(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 4);
    module->globals = stasis_allocate_zeroed(count, sizeof *module->globals);
    if (module->globals == NULL) {
        return MODULE_READ_OUT_OF_MEMORY;
    }
    module->global_count = count;
    for (uint32_t i = 0; i < count; i++) {
        struct module_global *global = &module->globals[i];
        global->type = read_byte(reader);
        const uint8_t mutability = read_byte(reader);
        if (mutability > 1) {
            reader->failed = true;
        }
        global->is_mutable = mutability == 1;
        uint32_t value = 0;
        (void)read_constant_expression(reader, &value);
    }
    return MODULE_READ_OK;
}

/* Whether `index`, in an index space whose first `imported` entries are imports, is one of the
 * `count` entries after them that the module defines. If so, `defined` is set to its position among
 * them. */
static bool is_defined(uint32_t index, uint32_t imported, uint32_t count, uint32_t *defined) {
    if (index < imported || index - imported >= count) {
        return false;
    }
    *defined = index - imported;
    return true;
}

/* Marks the memory or the global an export exports as exported, if the module defines it. The
 * memory and global sections come before the export section. */
static void mark_exported(struct module *module, const struct module_export *export) {
    uint32_t defined = 0;
    if (export->kind == MODULE_MEMORY &&
        is_defined(export->index, module->memory_import_count, module->memory_count, &defined)) {
        module->memories[defined].exported = true;
    } else if (export->kind == MODULE_GLOBAL &&
               is_defined(export->index, module->global_import_count, module->global_count,
                          &defined)) {
        module->globals[defined].exported = true;
    }
}

/* Reads the export section: each export's name, kind and index. */
static enum module_read read_exports(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 3);
    module->exports = stasis_allocate_zeroed(count, sizeof *module->exports);
    if (module->exports == NULL) {
        return MODULE_READ_OUT_OF_MEMORY;
    }
    module->export_count = count;
    for (uint32_t i = 0; i < count; i++) {
        struct module_export *export = &module->exports[i];
        export->name = read_name(reader);
        export->kind = read_byte(reader);
        export->index = read_u32(reader);
        mark_exported(module, export);
    }
    return MODULE_READ_OK;
}

/* Reads the element section: where each segment goes and how many elements it holds. */
static enum module_read read_elements(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 3);
    module->element_segments = stasis_allocate_zeroed(count, sizeof *module->element_segments);
    if (module->element_segments == NULL) {
        return MODULE_READ_OUT_OF_MEMORY;
    }
    module->element_segment_count = count;
    for (uint32_t i = 0; i < count; i++) {
        struct module_segment *segment = &module->element_segments[i];
        const uint32_t flags = read_u32(reader);
        if (flags > (ELEMENT_NOT_ACTIVE | ELEMENT_TABLE_INDEX | ELEMENT_EXPRESSIONS)) {
            reader->failed = true;
        }
        segment->active = (flags & ELEMENT_NOT_ACTIVE) == 0;
        if (segment->active) {
            if ((flags & ELEMENT_TABLE_INDEX) != 0) {
                segment->index = read_u32(reader);
            }
            segment->constant_offset = read_constant_expression(reader, &segment->offset);
        }
        /* Every form but those with neither flag names the kind or the type of its elements. */
        if ((flags & (ELEMENT_NOT_ACTIVE | ELEMENT_TABLE_INDEX)) != 0) {
            (void)read_byte(reader);
        }
        const bool expressions = (flags & ELEMENT_EXPRESSIONS) != 0;
        segment->size = read_count(reader, expressions ? 2 : 1);
        for (uint32_t element = 0; element < segment->size; element++) {
            if (expressions) {
                uint32_t value = 0;
                (void)read_constant_expression(reader, &value);
            } else {
                (void)read_u32(reader);
            }
        }
    }
    return MODULE_READ_OK;
}

/* Reads the body of the function at `function` in the function index space: its locals, and every
 * instruction of its code, noting the first use of each that changes a table or drops a
 * segment. */
static void read_body(struct module *module, uint32_t function, struct reader *reader) {
    const uint32_t local_groups = read_count(reader, 2);
    for (uint32_t i = 0; i < local_groups; i++) {
        (void)read_u32(reader);
        (void)read_byte(reader);
    }
    while (!reader->failed && reader->at != reader->end) {
        const enum module_change change = read_immediates(reader, read_byte(reader));
        if (change != NO_CHANGE && !module->first_uses[change].used) {
            module->first_uses[change].used = true;
            module->first_uses[change].function = function;
        }
    }
}

/* Reads the code section: each function's body. */
static void read_code(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 3);
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t size = read_u32(reader);
        struct reader body;
        body.at = read_bytes(reader, size);
        body.end = body.at == NULL ? NULL : body.at + size;
        body.failed = body.at == NULL;
        read_body(module, module->function_import_count + i, &body);
        if (body.failed) {
            reader->failed = true;
        }
    }
}

/* Reads the data section: where each segment goes and how many bytes it holds. */
static enum module_read read_data(struct module *module, struct reader *reader) {
    const uint32_t count = read_count(reader, 2);
    module->data_segments = stasis_allocate_zeroed(count, sizeof *module->data_segments);
    if (module->data_segments == NULL) {
        return MODULE_READ_OUT_OF_MEMORY;
    }
    module->data_segment_count = count;
    for (uint32_t i = 0; i < count; i++) {
        struct module_segment *segment = &module->data_segments[i];
        const uint32_t flags = read_u32(reader);
        if (flags > DATA_MEMORY_INDEX) {
            reader->failed = true;
        }
        segment->active = flags != DATA_PASSIVE;
        if (segment->active) {
            if (flags == DATA_MEMORY_INDEX) {
                segment->index = read_u32(reader);
            }
            segment->constant_offset = read_constant_expression(reader, &segment->offset);
        }
        segment->size = read_u32(reader);
        (void)read_bytes(reader, segment->size);
    }
    return MODULE_READ_OK;
}

/* Reads the contents of a section the reader reads, which must hold nothing more. */
static enum module_read read_section(struct module *module, uint8_t id, struct reader *reader) {
    enum module_read result = MODULE_READ_OK;
    switch (id) {
    case SECTION_TYPE:
        result = read_types(module, reader);
        break;
    case SECTION_IMPORT:
        result = read_imports(module, reader);
        break;
    case SECTION_FUNCTION:
        result = read_functions(module, reader);
        break;
    case SECTION_TABLE:
        result = read_tables(module, reader);
        break;
    case SECTION_MEMORY:
        result = read_memories(module, reader);
        break;
    case SECTION_GLOBAL:
        result = read_globals(module, reader);
        break;
    case SECTION_EXPORT:
        result = read_exports(module, reader);
        break;
    case SECTION_START:
        (void)read_u32(reader);
        module->has_start = true;
        break;
    case SECTION_ELEMENT:
        result = read_elements(module, reader);
        break;
    case SECTION_CODE:
        read_code(module, reader);
        break;
    case SECTION_DATA:
        result = read_data(module, reader);
        break;
    default:
        return MODULE_READ_OK;
    }
    if (result == MODULE_READ_OK && (reader->failed || reader->at != reader->end)) {
        return MODULE_READ_MALFORMED;
    }
    return result;
}

/* Adds a section to the identity: its id, its size as the shortest LEB128 that holds it, and its
 * contents. So the identity doesn't depend on how a tool pads the sizes it writes. */
static void hash_section(stasis_sha256 *sha, uint8_t id, const uint8_t *contents, uint32_t size) {
    uint8_t header[6];
    size_t length = 0;
    header[length++] = id;
    uint32_t rest = size;
    do {
        uint8_t byte = (uint8_t)(rest & 0x7f);
        rest >>= 7;
        if (rest != 0) {
            byte |= 0x80;
        }
        header[length++] = byte;
    } while (rest != 0);
    stasis_sha256_update(sha, header, length);
    stasis_sha256_update(sha, contents, size);
}

enum module_read stasis_module_read(struct module *module, const uint8_t *bytes, size_t size) {
    static const uint8_t preamble[8] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    memset(module, 0, sizeof *module);
    if (size < sizeof preamble || memcmp(bytes, preamble, sizeof preamble) != 0) {
        return MODULE_READ_MALFORMED;
    }
    struct reader reader;
    reader.at = bytes + sizeof preamble;
    reader.end = bytes + size;
    reader.failed = false;
    stasis_sha256 sha;
    stasis_sha256_init(&sha);
    /* The ids of the sections read so far, other than custom ones, each of which may come once. */
    uint32_t seen = 0;
    while (reader.at != reader.end) {
        const uint8_t id = read_byte(&reader);
        const uint32_t section_size = read_u32(&reader);
        const uint8_t *contents = read_bytes(&reader, section_size);
        if (contents == NULL) {
            return MODULE_READ_MALFORMED;
        }
        if (id == SECTION_CUSTOM) {
            continue;
        }
        if (id < 32) {
            if ((seen & (UINT32_C(1) << id)) != 0) {
                return MODULE_READ_MALFORMED;
            }
            seen |= UINT32_C(1) << id;
        }
        hash_section(&sha, id, contents, section_size);
        struct reader section;
        section.at = contents;
        section.end = contents + section_size;
        section.failed = false;
        const enum module_read result = read_section(module, id, &section);
        if (result != MODULE_READ_OK) {
            return result;
        }
    }
    stasis_sha256_final(&sha, module->identity);
    return MODULE_READ_OK;
}

void stasis_module_free(struct module *module) {
    stasis_deallocate(module->types);
    stasis_deallocate(module->imports);
    stasis_deallocate(module->functions);
    stasis_deallocate(module->tables);
    stasis_deallocate(module->memories);
    stasis_deallocate(module->globals);
    stasis_deallocate(module->exports);
    stasis_deallocate(module->element_segments);
    stasis_deallocate(module->data_segments);
    memset(module, 0, sizeof *module);
}

const char *stasis_module_type_name(uint8_t type) {
    switch (type) {
    case MODULE_I32:
        return "i32";
    case MODULE_I64:
        return "i64";
    case MODULE_F32:
        return "f32";
    case MODULE_F64:
        return "f64";
    case MODULE_FUNCREF:
        return "funcref";
    case MODULE_EXTERNREF:
        return "externref";
    default:
        return "?";
    }
}

const struct module_table *stasis_module_table(const struct module *module, uint32_t index) {
    uint32_t defined = 0;
    if (!is_defined(index, module->table_import_count, module->table_count, &defined)) {
        return NULL;
    }
    return &module->tables[defined];
}

const struct module_memory *stasis_module_memory(const struct module *module, uint32_t index) {
    uint32_t defined = 0;
    if (!is_defined(index, module->memory_import_count, module->memory_count, &defined)) {
        return NULL;
    }
    return &module->memories[defined];
}

const struct module_function_type *stasis_module_function_type(const struct module *module,
                                                               uint32_t index) {
    uint32_t type = 0;
    if (index < module->function_import_count) {
        uint32_t function = 0;
        for (uint32_t i = 0; i < module->import_count; i++) {
            if (module->imports[i].kind != MODULE_FUNCTION) {
                continue;
            }
            if (function == index) {
                type = module->imports[i].type;
                break;
            }
            function++;
        }
    } else if (index - module->function_import_count < module->function_count) {
        type = module->functions[index - module->function_import_count];
    } else {
        return NULL;
    }
    return type < module->type_count ? &module->types[type] : NULL;
}
