#include "parser.h"

#include "arena.h"
#include "diagnostics.h"
#include "interface.h"
#include "lexer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The words that later versions of the format will give a meaning, which nothing can be called. */
static const char *const reserved_words[] = {
    "list", "tuple", "enum", "flags", "variant", "option", "result", "char",
};

/* The names of the built-in types, in the order of enum type_kind. */
static const char *const type_names[] = {
    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64", "bool", "string",
};

/* The state of the parser, with the token it's at. */
struct parser {
    struct lexer lexer;
    struct token token;
    /* The token after it, once peek has lexed it. */
    struct token next;
    bool peeked;
    /* Whether the token's doc comment documents the declaration the token starts. */
    bool doc_taken;
    struct arena *arena;
    struct diagnostics *diagnostics;
    struct interface *interface;
    /* Where the next group, kind of script, and item of the group being parsed go. */
    struct group **next_group;
    struct script **next_script;
    struct item **next_item;
    /* Where the kind of script being parsed has its `uses`, once it has. */
    struct position uses;
    bool has_uses;
};

/* Whether the token is the word `word`. */
static bool token_is(const struct token *token, const char *word) {
    if (token->kind != TOKEN_NAME || strlen(word) != token->length) {
        return false;
    }
    return memcmp(word, token->text, token->length) == 0;
}

/* Whether the token is a reserved word. */
static bool is_reserved(const struct token *token) {
    for (size_t i = 0; i < sizeof reserved_words / sizeof reserved_words[0]; i++) {
        if (token_is(token, reserved_words[i])) {
            return true;
        }
    }
    return false;
}

/* The built-in type the token names, or TYPE_NAMED if it names none. */
static enum type_kind built_in_type(const struct token *token) {
    for (size_t i = 0; i < sizeof type_names / sizeof type_names[0]; i++) {
        if (token_is(token, type_names[i])) {
            return (enum type_kind)i;
        }
    }
    return TYPE_NAMED;
}

/* The token as a message names it. */
static const char *describe(struct parser *parser) {
    const struct token *token = &parser->token;
    if (token->kind == TOKEN_END) {
        return token_spelling(TOKEN_END);
    }
    return arena_format(parser->arena, "`%.*s`", (int)token->length, token->text);
}

/* Reports that `what` was expected where the token is. */
static void expected(struct parser *parser, const char *what) {
    report_error(parser->diagnostics, parser->token.position, "expected %s, found %s", what,
                 describe(parser));
}

/* Moves on to the next token, without checking the one it leaves. */
static void skip_token(struct parser *parser) {
    parser->token = parser->peeked ? parser->next : lexer_next(&parser->lexer);
    parser->peeked = false;
    parser->doc_taken = false;
}

/* The kind of the token after the current one. */
static enum token_kind peek(struct parser *parser) {
    if (!parser->peeked) {
        parser->next = lexer_next(&parser->lexer);
        parser->peeked = true;
    }
    return parser->next.kind;
}

/* Whether the token ends a list early: the end of the file, or a keyword that starts the next
 * declaration rather than naming an item, as a keyword before a `:` would. */
static bool ends_list(struct parser *parser) {
    if (parser->token.kind == TOKEN_END) {
        return true;
    }
    if (!is_keyword(parser->token.kind)) {
        return false;
    }
    return peek(parser) != TOKEN_COLON;
}

/* Moves on to the next token, as parsing does, reporting a doc comment before the one it leaves
 * that documents nothing. Skipping after an error moves on with skip_token, which doesn't. */
static void advance(struct parser *parser) {
    if (parser->token.doc.count > 0 && !parser->doc_taken) {
        report_error(parser->diagnostics, parser->token.doc_position,
                     "this doc comment documents nothing");
    }
    skip_token(parser);
}

/* Takes the token's doc comment for the declaration it starts. */
static struct doc take_doc(struct parser *parser) {
    parser->doc_taken = true;
    return parser->token.doc;
}

/* Steps past the token if it's of `kind`, and says whether it was. */
static bool accept(struct parser *parser, enum token_kind kind) {
    if (parser->token.kind != kind) {
        return false;
    }
    advance(parser);
    return true;
}

/* Steps past the token if it's of `kind`, and reports it if it isn't. */
static bool expect(struct parser *parser, enum token_kind kind) {
    if (accept(parser, kind)) {
        return true;
    }
    expected(parser, arena_format(parser->arena, "`%s`", token_spelling(kind)));
    return false;
}

/* Whether a token of `kind` opens a bracket. */
static bool is_opening(enum token_kind kind) {
    switch (kind) {
    case TOKEN_LEFT_BRACE:
    case TOKEN_LEFT_PARENTHESIS:
    case TOKEN_LEFT_ANGLE:
        return true;
    default:
        return false;
    }
}

/* Whether a token of `kind` closes a bracket. */
static bool is_closing(enum token_kind kind) {
    switch (kind) {
    case TOKEN_RIGHT_BRACE:
    case TOKEN_RIGHT_PARENTHESIS:
    case TOKEN_RIGHT_ANGLE:
        return true;
    default:
        return false;
    }
}

/* Skips tokens after an error, up to where parsing can pick up again. In a list, that's the `,`
 * after the item, or the bracket that closes the list. Elsewhere, it's past the `;` that ends the
 * declaration, or past the `}` that ends its body and any `;` after that, as a `use` has. Either
 * way, skipping stops early at a keyword, which starts the next declaration, at a bracket that
 * closes what the declaration is in, and at the end of the file. Brackets inside what's skipped
 * are skipped whole. */
static void skip(struct parser *parser, bool in_list) {
    size_t depth = 0;
    for (;;) {
        const enum token_kind kind = parser->token.kind;
        if (kind == TOKEN_END || (depth == 0 && is_keyword(kind))) {
            return;
        }
        if (is_closing(kind)) {
            if (depth == 0) {
                return;
            }
            depth--;
            if (depth == 0 && kind == TOKEN_RIGHT_BRACE && !in_list) {
                skip_token(parser);
                if (parser->token.kind == TOKEN_SEMICOLON) {
                    skip_token(parser);
                }
                return;
            }
        } else if (is_opening(kind)) {
            depth++;
        } else if (depth == 0 && kind == TOKEN_COMMA && in_list) {
            return;
        } else if (depth == 0 && kind == TOKEN_SEMICOLON && !in_list) {
            skip_token(parser);
            return;
        }
        skip_token(parser);
    }
}

/* Skips the rest of a declaration after an error. */
static void skip_declaration(struct parser *parser) { skip(parser, false); }

/* Reports the token, which is a reserved word. */
static void report_reserved(struct parser *parser) {
    report_error(parser->diagnostics, parser->token.position,
                 "`%.*s` is reserved for a later version of the format", (int)parser->token.length,
                 parser->token.text);
}

/* Reports a token that can't start a declaration where it is, and skips the declaration it starts,
 * if it does start one. */
static void unexpected_declaration(struct parser *parser, const char *what) {
    if (is_reserved(&parser->token)) {
        report_reserved(parser);
    } else {
        expected(parser, what);
    }
    skip_token(parser);
    skip_declaration(parser);
}

/* After an item of a list that ends at `closing`, steps past the `,` before the next item. Returns
 * whether the list goes on, which it doesn't if neither a `,` nor `closing` follows. That is
 * reported, unless `reported` says the item has been already. */
static bool continue_list(struct parser *parser, enum token_kind closing, bool reported) {
    if (!reported && parser->token.kind != TOKEN_COMMA && parser->token.kind != closing) {
        expected(parser, arena_format(parser->arena, "`,` or `%s`", token_spelling(closing)));
        skip(parser, true);
    }
    if (accept(parser, TOKEN_COMMA)) {
        return true;
    }
    return parser->token.kind == closing;
}

/* What's wrong with a name's spelling, as the end of a message, or null if nothing is. */
static const char *name_problem(const char *text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (text[i] >= 'A' && text[i] <= 'Z') {
            return "names are lowercase";
        }
    }
    if (text[0] == '_') {
        return "names start with a letter";
    }
    if (text[length - 1] == '_') {
        return "names don't end in `_`";
    }
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '_' && text[i + 1] == '_') {
            return "the words of a name are separated by a single `_`";
        }
        if (text[i] == '_' && text[i + 1] >= '0' && text[i + 1] <= '9') {
            return "each word of a name starts with a letter";
        }
    }
    return NULL;
}

/* Reports the name a declaration declares if it can't be one. */
static void check_name(struct parser *parser) {
    const struct token *token = &parser->token;
    const int length = (int)token->length;
    if (token->reported) {
        return;
    }
    if (is_reserved(token)) {
        report_reserved(parser);
        return;
    }
    if (built_in_type(token) != TYPE_NAMED) {
        report_error(parser->diagnostics, token->position,
                     "`%.*s` is a built-in type, and can't be a name", length, token->text);
        return;
    }
    const char *problem = name_problem(token->text, token->length);
    if (problem != NULL) {
        report_error(parser->diagnostics, token->position, "`%.*s` isn't a valid name: %s", length,
                     token->text, problem);
    }
}

/* Copies the token as a name, and steps past it. */
static void take_name(struct parser *parser, struct name *name) {
    name->text = arena_copy(parser->arena, parser->token.text, parser->token.length);
    name->position = parser->token.position;
    advance(parser);
}

/* Parses the name a declaration declares, reporting one that can't be a name. A keyword is taken
 * as the name, once reported, so that the rest of the declaration still parses. Returns false if
 * there's no name, which it reports. */
static bool parse_declared_name(struct parser *parser, struct name *name) {
    if (parser->token.kind == TOKEN_NAME) {
        check_name(parser);
    } else if (is_keyword(parser->token.kind)) {
        report_error(parser->diagnostics, parser->token.position,
                     "`%s` is a keyword, and can't be a name", token_spelling(parser->token.kind));
    } else {
        expected(parser, "a name");
        return false;
    }
    take_name(parser, name);
    return true;
}

/* Parses a name that refers to a declaration, which is `what`. */
static bool parse_reference(struct parser *parser, struct name *name, const char *what) {
    if (parser->token.kind != TOKEN_NAME) {
        expected(parser, what);
        return false;
    }
    take_name(parser, name);
    return true;
}

/* Skips the type arguments that follow a type's name, from their `<` to their `>`. */
static void skip_type_arguments(struct parser *parser) {
    size_t depth = 0;
    for (;;) {
        const enum token_kind kind = parser->token.kind;
        if (kind == TOKEN_LEFT_ANGLE) {
            depth++;
        } else if (kind == TOKEN_RIGHT_ANGLE) {
            depth--;
            if (depth == 0) {
                skip_token(parser);
                return;
            }
        } else if (kind == TOKEN_END || is_keyword(kind) || kind == TOKEN_SEMICOLON ||
                   kind == TOKEN_LEFT_BRACE || kind == TOKEN_RIGHT_BRACE ||
                   kind == TOKEN_RIGHT_PARENTHESIS) {
            return;
        }
        skip_token(parser);
    }
}

/* Parses a type: a built-in type's name, or the name of a record or object, which the checker
 * resolves. */
static bool parse_type(struct parser *parser, struct type *type) {
    const struct token *token = &parser->token;
    if (token->kind != TOKEN_NAME) {
        expected(parser, "a type");
        return false;
    }
    if (token->reported) {
        type->kind = TYPE_INVALID;
    } else if (is_reserved(token)) {
        report_reserved(parser);
        type->kind = TYPE_INVALID;
    } else {
        type->kind = built_in_type(token);
    }
    take_name(parser, &type->name);
    if (parser->token.kind == TOKEN_LEFT_ANGLE) {
        if (type->kind != TYPE_INVALID) {
            report_error(parser->diagnostics, parser->token.position,
                         "type arguments after `%s`, but no type takes any yet", type->name.text);
            type->kind = TYPE_INVALID;
        }
        skip_type_arguments(parser);
    }
    return true;
}

/* Adds an item of `kind` to the group being parsed. */
static struct item *add_item(struct parser *parser, enum item_kind kind) {
    struct item *item = arena_allocate(parser->arena, sizeof *item);
    item->kind = kind;
    *parser->next_item = item;
    parser->next_item = &item->next;
    return item;
}

/* Parses one of a record's fields, up to its type. */
static struct field *parse_field(struct parser *parser) {
    struct field *field = arena_allocate(parser->arena, sizeof *field);
    field->doc = take_doc(parser);
    if (!parse_declared_name(parser, &field->name) || !expect(parser, TOKEN_COLON) ||
        !parse_type(parser, &field->type)) {
        return NULL;
    }
    return field;
}

/* Parses a record, from its `record`. */
static void parse_record(struct parser *parser, struct group *group) {
    struct record *record = arena_allocate(parser->arena, sizeof *record);
    record->doc = take_doc(parser);
    advance(parser);
    if (!parse_declared_name(parser, &record->name)) {
        skip_declaration(parser);
        return;
    }
    record->group = group;
    record->index = parser->interface->record_count++;
    add_item(parser, ITEM_RECORD)->as.record = record;
    if (!expect(parser, TOKEN_LEFT_BRACE)) {
        skip_declaration(parser);
        return;
    }

    struct field **next = &record->fields;
    bool reported = false;
    while (parser->token.kind != TOKEN_RIGHT_BRACE) {
        if (ends_list(parser)) {
            expected(parser, "a field or `}`");
            return;
        }
        struct field *field = parse_field(parser);
        if (field == NULL) {
            reported = true;
            skip(parser, true);
        } else {
            *next = field;
            next = &field->next;
        }
        if (!continue_list(parser, TOKEN_RIGHT_BRACE, field == NULL)) {
            return;
        }
    }
    if (record->fields == NULL && !reported) {
        report_error(parser->diagnostics, record->name.position,
                     "record `%s` has no fields, but a record needs at least one",
                     record->name.text);
    }
    advance(parser);
}

/* Parses one of a function's parameters. */
static struct parameter *parse_parameter(struct parser *parser) {
    struct parameter *parameter = arena_allocate(parser->arena, sizeof *parameter);
    if (!parse_declared_name(parser, &parameter->name) || !expect(parser, TOKEN_COLON) ||
        !parse_type(parser, &parameter->type)) {
        return NULL;
    }
    return parameter;
}

/* Parses a function's parameters, after their `(` and up to their `)`. Returns false if they
 * don't parse as a list, having reported it. */
static bool parse_parameters(struct parser *parser, struct function *function) {
    struct parameter **next = &function->parameters;
    while (parser->token.kind != TOKEN_RIGHT_PARENTHESIS) {
        if (ends_list(parser)) {
            expected(parser, "a parameter or `)`");
            return false;
        }
        struct parameter *parameter = parse_parameter(parser);
        if (parameter == NULL) {
            skip(parser, true);
        } else {
            *next = parameter;
            next = &parameter->next;
        }
        if (!continue_list(parser, TOKEN_RIGHT_PARENTHESIS, parameter == NULL)) {
            return false;
        }
    }
    advance(parser);
    return true;
}

/* Parses a host function, a method or an entry point, from its `fn`, documented by `doc`. Returns
 * null if it has no name. */
static struct function *parse_function(struct parser *parser, struct doc doc) {
    struct function *function = arena_allocate(parser->arena, sizeof *function);
    function->doc = doc;
    advance(parser);
    if (!parse_declared_name(parser, &function->name)) {
        skip_declaration(parser);
        return NULL;
    }
    if (!expect(parser, TOKEN_LEFT_PARENTHESIS) || !parse_parameters(parser, function)) {
        skip_declaration(parser);
        return function;
    }
    if (accept(parser, TOKEN_ARROW)) {
        function->has_result = true;
        if (!parse_type(parser, &function->result)) {
            function->result.kind = TYPE_INVALID;
            skip_declaration(parser);
            return function;
        }
    }
    if (!expect(parser, TOKEN_SEMICOLON)) {
        skip_declaration(parser);
    }
    return function;
}

/* Parses an object, from its `object`. */
static void parse_object(struct parser *parser, struct group *group) {
    struct object *object = arena_allocate(parser->arena, sizeof *object);
    object->doc = take_doc(parser);
    advance(parser);
    if (!parse_declared_name(parser, &object->name)) {
        skip_declaration(parser);
        return;
    }
    object->group = group;
    add_item(parser, ITEM_OBJECT)->as.object = object;
    if (!expect(parser, TOKEN_LEFT_BRACE)) {
        skip_declaration(parser);
        return;
    }

    struct function **next = &object->methods;
    for (;;) {
        switch (parser->token.kind) {
        case TOKEN_RIGHT_BRACE:
            advance(parser);
            return;
        case TOKEN_FN: {
            struct function *method = parse_function(parser, take_doc(parser));
            if (method != NULL) {
                method->object = object;
                *next = method;
                next = &method->next;
            }
            break;
        }
        case TOKEN_OPTIONAL:
            report_error(parser->diagnostics, parser->token.position,
                         "only an entry point can be optional");
            skip_token(parser);
            break;
        case TOKEN_END:
        case TOKEN_INTERFACE:
        case TOKEN_GROUP:
        case TOKEN_SCRIPT:
        case TOKEN_RECORD:
        case TOKEN_OBJECT:
        case TOKEN_USE:
        case TOKEN_USES:
            expected(parser, arena_format(parser->arena, "a method, or `}` to end object `%s`",
                                          object->name.text));
            return;
        default:
            unexpected_declaration(parser, "a method or `}`");
            break;
        }
    }
}

/* Parses a `use`, from its `use`. */
static void parse_use(struct parser *parser) {
    struct use *use = arena_allocate(parser->arena, sizeof *use);
    advance(parser);
    if (!parse_reference(parser, &use->group_name, "the name of a group")) {
        skip_declaration(parser);
        return;
    }
    add_item(parser, ITEM_USE)->as.use = use;
    if (!expect(parser, TOKEN_DOUBLE_COLON) || !expect(parser, TOKEN_LEFT_BRACE)) {
        skip_declaration(parser);
        return;
    }

    struct use_item **next = &use->items;
    do {
        struct use_item *item = arena_allocate(parser->arena, sizeof *item);
        bool reported = false;
        if (parse_reference(parser, &item->name, "the name of a record or object")) {
            *next = item;
            next = &item->next;
        } else {
            reported = true;
            skip(parser, true);
        }
        if (!continue_list(parser, TOKEN_RIGHT_BRACE, reported)) {
            skip_declaration(parser);
            return;
        }
    } while (parser->token.kind != TOKEN_RIGHT_BRACE);
    advance(parser);
    if (!expect(parser, TOKEN_SEMICOLON)) {
        skip_declaration(parser);
    }
}

/* Parses a group, from its `group`. */
static void parse_group(struct parser *parser) {
    struct group *group = arena_allocate(parser->arena, sizeof *group);
    group->doc = take_doc(parser);
    advance(parser);
    if (!parse_declared_name(parser, &group->name)) {
        skip_declaration(parser);
        return;
    }
    group->index = parser->interface->group_count++;
    *parser->next_group = group;
    parser->next_group = &group->next;
    parser->next_item = &group->items;
    if (!expect(parser, TOKEN_LEFT_BRACE)) {
        skip_declaration(parser);
        return;
    }

    for (;;) {
        switch (parser->token.kind) {
        case TOKEN_RIGHT_BRACE:
            advance(parser);
            return;
        case TOKEN_RECORD:
            parse_record(parser, group);
            break;
        case TOKEN_OBJECT:
            parse_object(parser, group);
            break;
        case TOKEN_FN: {
            struct function *function = parse_function(parser, take_doc(parser));
            if (function != NULL) {
                add_item(parser, ITEM_FUNCTION)->as.function = function;
            }
            break;
        }
        case TOKEN_USE:
            parse_use(parser);
            break;
        case TOKEN_OPTIONAL:
            report_error(parser->diagnostics, parser->token.position,
                         "only an entry point can be optional");
            skip_token(parser);
            break;
        case TOKEN_USES:
            report_error(parser->diagnostics, parser->token.position,
                         "`uses` belongs in a kind of script: a group brings in records and "
                         "objects with `use`");
            skip_token(parser);
            skip_declaration(parser);
            break;
        case TOKEN_END:
        case TOKEN_INTERFACE:
        case TOKEN_GROUP:
        case TOKEN_SCRIPT:
            expected(parser,
                     arena_format(parser->arena, "`}` to end group `%s`", group->name.text));
            return;
        default:
            unexpected_declaration(parser, "a record, an object, a function, `use` or `}`");
            break;
        }
    }
}

/* Parses a kind of script's `uses`, from its `uses`, adding the groups it names to the script's. */
static void parse_uses(struct parser *parser, struct script *script) {
    if (parser->has_uses) {
        report_error(parser->diagnostics, parser->token.position,
                     "another `uses`, but a kind of script has only one");
        report_note(parser->diagnostics, parser->uses, "the first is here");
    } else {
        parser->uses = parser->token.position;
        parser->has_uses = true;
    }
    struct script_use **next = &script->uses;
    while (*next != NULL) {
        next = &(*next)->next;
    }
    advance(parser);
    do {
        struct script_use *use = arena_allocate(parser->arena, sizeof *use);
        if (!parse_reference(parser, &use->name, "the name of a group")) {
            skip_declaration(parser);
            return;
        }
        *next = use;
        next = &use->next;
        if (!accept(parser, TOKEN_COMMA) && parser->token.kind != TOKEN_SEMICOLON) {
            expected(parser, "`,` or `;`");
            skip_declaration(parser);
            return;
        }
    } while (parser->token.kind != TOKEN_SEMICOLON);
    advance(parser);
}

/* Parses an entry point, from its `optional` or its `fn`. */
static struct function *parse_entry_point(struct parser *parser) {
    const struct doc doc = take_doc(parser);
    const bool optional = accept(parser, TOKEN_OPTIONAL);
    if (parser->token.kind != TOKEN_FN) {
        expected(parser, "`fn`");
        skip_declaration(parser);
        return NULL;
    }
    struct function *entry_point = parse_function(parser, doc);
    if (entry_point != NULL) {
        entry_point->optional = optional;
    }
    return entry_point;
}

/* Parses a kind of script, from its `script`. */
static void parse_script(struct parser *parser) {
    struct script *script = arena_allocate(parser->arena, sizeof *script);
    script->doc = take_doc(parser);
    parser->has_uses = false;
    advance(parser);
    if (!parse_declared_name(parser, &script->name)) {
        skip_declaration(parser);
        return;
    }
    *parser->next_script = script;
    parser->next_script = &script->next;
    if (!expect(parser, TOKEN_LEFT_BRACE)) {
        skip_declaration(parser);
        return;
    }

    struct function **next_entry_point = &script->entry_points;
    for (;;) {
        switch (parser->token.kind) {
        case TOKEN_RIGHT_BRACE:
            advance(parser);
            return;
        case TOKEN_USES:
            parse_uses(parser, script);
            break;
        case TOKEN_OPTIONAL:
        case TOKEN_FN: {
            struct function *entry_point = parse_entry_point(parser);
            if (entry_point != NULL) {
                *next_entry_point = entry_point;
                next_entry_point = &entry_point->next;
            }
            break;
        }
        case TOKEN_RECORD:
        case TOKEN_OBJECT:
        case TOKEN_USE:
            report_error(parser->diagnostics, parser->token.position,
                         "a kind of script declares only its `uses` and its entry points");
            skip_token(parser);
            skip_declaration(parser);
            break;
        case TOKEN_END:
        case TOKEN_INTERFACE:
        case TOKEN_GROUP:
        case TOKEN_SCRIPT:
            expected(parser, arena_format(parser->arena,
                                          "an entry point, `uses`, or `}` to end script `%s`",
                                          script->name.text));
            return;
        default:
            unexpected_declaration(parser, "an entry point, `uses` or `}`");
            break;
        }
    }
}

/* Parses one of a version's numbers. */
static bool parse_version_number(struct parser *parser, uint32_t *number) {
    const struct token *token = &parser->token;
    if (token->kind != TOKEN_NUMBER) {
        expected(parser, "a version number");
        return false;
    }
    const int length = (int)token->length;
    uint64_t value = 0;
    for (size_t i = 0; i < token->length; i++) {
        const char digit = token->text[i];
        if (digit < '0' || digit > '9') {
            report_error(parser->diagnostics, token->position, "`%.*s` isn't a version number",
                         length, token->text);
            skip_token(parser);
            return true;
        }
        value = value > UINT32_MAX ? value : (10 * value) + (uint64_t)(digit - '0');
    }
    if (token->length > 1 && token->text[0] == '0') {
        report_error(parser->diagnostics, token->position,
                     "`%.*s` has a leading zero, which a version number can't have", length,
                     token->text);
    } else if (value > UINT32_MAX) {
        report_error(parser->diagnostics, token->position,
                     "`%.*s` is too large: a version number is at most %lu", length, token->text,
                     (unsigned long)UINT32_MAX);
    }
    *number = (uint32_t)value;
    skip_token(parser);
    return true;
}

/* Parses the interface's version: three numbers, with nothing after them. */
static bool parse_version(struct parser *parser, struct version *version) {
    if (!parse_version_number(parser, &version->major) || !expect(parser, TOKEN_DOT) ||
        !parse_version_number(parser, &version->minor) || !expect(parser, TOKEN_DOT) ||
        !parse_version_number(parser, &version->patch)) {
        return false;
    }
    if (parser->token.kind == TOKEN_MINUS) {
        report_error(parser->diagnostics, parser->token.position,
                     "an interface's version can't have a pre-release suffix");
        return false;
    }
    if (parser->token.kind == TOKEN_PLUS) {
        report_error(parser->diagnostics, parser->token.position,
                     "an interface's version can't have build metadata");
        return false;
    }
    return true;
}

/* Parses the interface's header, `interface <namespace>:<name>@<version>;`. */
static void parse_header(struct parser *parser) {
    struct interface *interface = parser->interface;
    interface->position = parser->token.position;
    if (parser->token.kind != TOKEN_INTERFACE) {
        expected(parser, "the interface's header, `interface <namespace>:<name>@<version>;`");
        if (!is_keyword(parser->token.kind) && parser->token.kind != TOKEN_END) {
            skip_token(parser);
            skip_declaration(parser);
        }
        return;
    }
    interface->doc = take_doc(parser);
    advance(parser);
    if (!parse_declared_name(parser, &interface->space) || !expect(parser, TOKEN_COLON) ||
        !parse_declared_name(parser, &interface->name) || !expect(parser, TOKEN_AT) ||
        !parse_version(parser, &interface->version) || !expect(parser, TOKEN_SEMICOLON)) {
        skip_declaration(parser);
    }
}

struct interface *parse_interface(const char *source, size_t size, struct arena *arena,
                                  struct diagnostics *diagnostics) {
    struct parser parser;
    memset(&parser, 0, sizeof parser);
    lexer_init(&parser.lexer, source, size, arena, diagnostics);
    parser.arena = arena;
    parser.diagnostics = diagnostics;
    parser.interface = arena_allocate(arena, sizeof *parser.interface);
    parser.next_group = &parser.interface->groups;
    parser.next_script = &parser.interface->scripts;
    skip_token(&parser);

    parse_header(&parser);
    while (parser.token.kind != TOKEN_END) {
        switch (parser.token.kind) {
        case TOKEN_GROUP:
            parse_group(&parser);
            break;
        case TOKEN_SCRIPT:
            parse_script(&parser);
            break;
        case TOKEN_INTERFACE:
            report_error(diagnostics, parser.token.position,
                         "the interface's header comes once, before everything else");
            skip_token(&parser);
            skip_declaration(&parser);
            break;
        default:
            unexpected_declaration(&parser, "`group` or `script`");
            break;
        }
    }
    /* A doc comment at the end of the file documents nothing. */
    advance(&parser);
    return parser.interface;
}
