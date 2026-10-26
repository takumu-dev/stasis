/* Splits an interface file into tokens.
 *
 * The lexer reports what no token can hold: non-ASCII outside comments, invalid UTF-8 in comments,
 * block comments and characters the format doesn't use. It skips each of them and carries on. */

#ifndef STASIS_GENERATOR_LEXER_H
#define STASIS_GENERATOR_LEXER_H

#include "arena.h"
#include "diagnostics.h"
#include "interface.h"

#include <stdbool.h>
#include <stddef.h>

/* What a token is. */
enum token_kind {
    TOKEN_END,
    /* A word that starts with a letter or `_`. */
    TOKEN_NAME,
    /* A word that starts with a digit. */
    TOKEN_NUMBER,
    /* The keywords. */
    TOKEN_FN,
    TOKEN_GROUP,
    TOKEN_INTERFACE,
    TOKEN_OBJECT,
    TOKEN_OPTIONAL,
    TOKEN_RECORD,
    TOKEN_SCRIPT,
    TOKEN_USE,
    TOKEN_USES,
    /* The punctuation. */
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_PARENTHESIS,
    TOKEN_RIGHT_PARENTHESIS,
    TOKEN_LEFT_ANGLE,
    TOKEN_RIGHT_ANGLE,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_COLON,
    TOKEN_DOUBLE_COLON,
    TOKEN_DOT,
    TOKEN_AT,
    TOKEN_ARROW,
    TOKEN_MINUS,
    TOKEN_PLUS,
};

/* A token, and the doc comment before it. */
struct token {
    enum token_kind kind;
    /* The token's text, which points into the file. */
    const char *text;
    size_t length;
    struct position position;
    /* The doc comment right before it, if any, and where that starts. */
    struct doc doc;
    struct position doc_position;
    /* Whether the lexer has reported a problem with the token, as with a word that holds
     * non-ASCII. */
    bool reported;
};

/* The state of the lexer, which lexer_init sets up. */
struct lexer {
    const char *source;
    size_t size;
    size_t offset;
    size_t line;
    /* Where the line the lexer is on starts. */
    size_t line_start;
    struct arena *arena;
    struct diagnostics *diagnostics;
    /* The doc comment gathered so far, for the next token. */
    const char **doc_lines;
    size_t doc_count;
    size_t doc_capacity;
    struct position doc_position;
};

/* Starts lexing the `size` bytes of `source`. */
void lexer_init(struct lexer *lexer, const char *source, size_t size, struct arena *arena,
                struct diagnostics *diagnostics);

/* Returns the next token, which is TOKEN_END at the end of the file and from then on. */
struct token lexer_next(struct lexer *lexer);

/* How a token of `kind` is spelt, or what it is, for TOKEN_END, TOKEN_NAME and TOKEN_NUMBER. */
const char *token_spelling(enum token_kind kind);

/* Whether `kind` is a keyword. */
bool is_keyword(enum token_kind kind);

#endif
