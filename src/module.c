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
    SECTION_EXPORT = 7,
};

/* The form that starts a function type. */
enum { FUNCTION_TYPE = 0x60 };

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

/* Skips a table's or a memory's limits: a flag, the minimum, and the maximum if the flag says
 * there is one. */
static void skip_limits(struct reader *reader) {
    const uint8_t flags = read_byte(reader);
    if (flags > 3) {
        reader->failed = true;
    }
    (void)read_u32(reader);
    if ((flags & 1) != 0) {
        (void)read_u32(reader);
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
            skip_limits(reader);
            break;
        case MODULE_MEMORY:
            skip_limits(reader);
            break;
        case MODULE_GLOBAL:
            (void)read_byte(reader);
            (void)read_byte(reader);
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
    }
    return MODULE_READ_OK;
}

/* Reads the contents of a section the reader reads, which must hold nothing more, and must be the
 * only section with its id. */
static enum module_read read_section(struct module *module, uint8_t id, struct reader *reader) {
    if ((id == SECTION_TYPE && module->types != NULL) ||
        (id == SECTION_IMPORT && module->imports != NULL) ||
        (id == SECTION_FUNCTION && module->functions != NULL) ||
        (id == SECTION_EXPORT && module->exports != NULL)) {
        return MODULE_READ_MALFORMED;
    }
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
    case SECTION_EXPORT:
        result = read_exports(module, reader);
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
    stasis_deallocate(module->exports);
    memset(module, 0, sizeof *module);
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
