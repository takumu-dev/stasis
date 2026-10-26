#include "signatures.h"

#include "arena.h"
#include "interface.h"
#include "signature.h"

#include <stddef.h>
#include <string.h>

/* The encoding's scalar for each of the interface's. */
static const stasis_signature_scalar scalars[] = {
    [TYPE_U8] = STASIS_SIGNATURE_U8,     [TYPE_U16] = STASIS_SIGNATURE_U16,
    [TYPE_U32] = STASIS_SIGNATURE_U32,   [TYPE_U64] = STASIS_SIGNATURE_U64,
    [TYPE_S8] = STASIS_SIGNATURE_S8,     [TYPE_S16] = STASIS_SIGNATURE_S16,
    [TYPE_S32] = STASIS_SIGNATURE_S32,   [TYPE_S64] = STASIS_SIGNATURE_S64,
    [TYPE_F32] = STASIS_SIGNATURE_F32,   [TYPE_F64] = STASIS_SIGNATURE_F64,
    [TYPE_BOOL] = STASIS_SIGNATURE_BOOL, [TYPE_STRING] = STASIS_SIGNATURE_STRING,
};

/* Adds a type that isn't a record. */
static void add_scalar_or_handle(stasis_signature *signature, const struct type *type) {
    if (type->kind == TYPE_OBJECT) {
        stasis_signature_add_handle(signature, type->object->group->name.text,
                                    type->object->name.text);
    } else {
        stasis_signature_add_scalar(signature, scalars[type->kind]);
    }
}

/* Adds a type. A record's fields are added in turn, and those of the records they hold within
 * theirs, keeping the field that comes next in each record on a stack. */
static void add_type(stasis_signature *signature, const struct type *type, struct arena *arena) {
    if (type->kind != TYPE_RECORD) {
        add_scalar_or_handle(signature, type);
        return;
    }
    const struct field **next = NULL;
    size_t capacity = 0;
    next = (const struct field **)arena_grow(arena, (void *)next, 0, &capacity, sizeof *next);
    size_t depth = 1;
    next[0] = type->record->fields;
    stasis_signature_begin_record(signature);
    while (depth > 0) {
        const struct field *field = next[depth - 1];
        if (field == NULL) {
            stasis_signature_end_record(signature);
            depth--;
            continue;
        }
        next[depth - 1] = field->next;
        stasis_signature_begin_field(signature, field->name.text);
        if (field->type.kind != TYPE_RECORD) {
            add_scalar_or_handle(signature, &field->type);
            continue;
        }
        next =
            (const struct field **)arena_grow(arena, (void *)next, depth, &capacity, sizeof *next);
        next[depth++] = field->type.record->fields;
        stasis_signature_begin_record(signature);
    }
}

/* Encodes a function's signature into `text`, which has room for `capacity` bytes, and writes its
 * hash. Returns the length of its encoding. */
static size_t encode(const struct function *function, struct arena *arena, char *text,
                     size_t capacity, char *hash) {
    stasis_signature signature;
    stasis_signature_init(&signature, text, capacity);
    if (function->object != NULL) {
        stasis_signature_add_handle(&signature, function->object->group->name.text,
                                    function->object->name.text);
    }
    for (const struct parameter *parameter = function->parameters; parameter != NULL;
         parameter = parameter->next) {
        add_type(&signature, &parameter->type, arena);
    }
    if (function->has_result) {
        stasis_signature_begin_result(&signature);
        add_type(&signature, &function->result, arena);
    }
    return stasis_signature_finish(&signature, hash);
}

void encode_signature(const struct function *function, struct arena *arena,
                      struct signature *signature) {
    const size_t length = encode(function, arena, NULL, 0, signature->hash);
    char *text = arena_allocate(arena, length + 1);
    encode(function, arena, text, length + 1, signature->hash);
    signature->encoding = text;
}
