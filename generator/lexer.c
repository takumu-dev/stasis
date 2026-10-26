#include "lexer.h"

#include "arena.h"
#include "diagnostics.h"
#include "interface.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The spelling of each kind of token, in the order of enum token_kind. */
static const char *const spellings[] = {
    "the end of the file",
    "a name",
    "a number",
    "fn",
    "group",
    "interface",
    "object",
    "optional",
    "record",
    "script",
    "use",
    "uses",
    "{",
    "}",
    "(",
    ")",
    "<",
    ">",
    ",",
    ";",
    ":",
    "::",
    ".",
    "@",
    "->",
    "-",
    "+",
};

const char *token_spelling(enum token_kind kind) { return spellings[kind]; }

bool is_keyword(enum token_kind kind) {
    if (kind < TOKEN_FN) {
        return false;
    }
    return kind <= TOKEN_USES;
}

void lexer_init(struct lexer *lexer, const char *source, size_t size, struct arena *arena,
                struct diagnostics *diagnostics) {
    memset(lexer, 0, sizeof *lexer);
    lexer->source = source;
    lexer->size = size;
    lexer->line = 1;
    lexer->arena = arena;
    lexer->diagnostics = diagnostics;
    /* A byte order mark is non-ASCII outside a comment, but it's reported as what it is, and then
     * skipped, so that the header after it still counts. */
    if (size >= 3 && memcmp(source, "\xef\xbb\xbf", 3) == 0) {
        report_error(diagnostics, (struct position){1, 1},
                     "the file starts with a byte order mark, which an interface file can't have");
        lexer->offset = 3;
    }
}

/* Where the byte at `offset` on the current line is. */
static struct position position_at(const struct lexer *lexer, size_t offset) {
    struct position position;
    position.line = lexer->line;
    position.column = offset - lexer->line_start + 1;
    return position;
}

/* The byte at `offset`, or 0 beyond the end. */
static unsigned char byte_at(const struct lexer *lexer, size_t offset) {
    return offset < lexer->size ? (unsigned char)lexer->source[offset] : 0;
}

/* The length of the UTF-8 sequence at `offset`, or 0 if it isn't valid UTF-8. */
static size_t utf8_length(const struct lexer *lexer, size_t offset) {
    const unsigned char first = byte_at(lexer, offset);
    size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xbf;
    if (first < 0x80) {
        return 1;
    }
    if (first >= 0xc2 && first <= 0xdf) {
        length = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
        length = 3;
        low = first == 0xe0 ? 0xa0 : 0x80;
        high = first == 0xed ? 0x9f : 0xbf;
    } else if (first >= 0xf0 && first <= 0xf4) {
        length = 4;
        low = first == 0xf0 ? 0x90 : 0x80;
        high = first == 0xf4 ? 0x8f : 0xbf;
    } else {
        return 0;
    }
    for (size_t i = 1; i < length; i++) {
        const unsigned char next = byte_at(lexer, offset + i);
        if (next < (i == 1 ? low : 0x80) || next > (i == 1 ? high : 0xbf)) {
            return 0;
        }
    }
    return length;
}

/* Skips a comment, from its // to the end of its line, reporting where any invalid UTF-8 in it
 * starts. Returns where its line ends. */
static size_t skip_comment(struct lexer *lexer) {
    size_t offset = lexer->offset;
    bool reported = false;
    while (offset < lexer->size && lexer->source[offset] != '\n') {
        const size_t length = utf8_length(lexer, offset);
        if (length == 0 && !reported) {
            report_error(lexer->diagnostics, position_at(lexer, offset),
                         "invalid UTF-8 in a comment");
            reported = true;
        }
        offset += length == 0 ? 1 : length;
    }
    lexer->offset = offset;
    return offset;
}

/* Adds a line of a doc comment, starting at its ///, to the doc comment being gathered. */
static void add_doc_line(struct lexer *lexer) {
    if (lexer->doc_count == 0) {
        lexer->doc_position = position_at(lexer, lexer->offset);
    }
    const size_t start = lexer->offset + 3;
    size_t end = skip_comment(lexer);
    if (end > start && lexer->source[end - 1] == '\r') {
        end--;
    }
    lexer->doc_lines =
        (const char **)arena_grow(lexer->arena, (void *)lexer->doc_lines, lexer->doc_count,
                                  &lexer->doc_capacity, sizeof *lexer->doc_lines);
    lexer->doc_lines[lexer->doc_count++] =
        arena_copy(lexer->arena, lexer->source + start, end > start ? end - start : 0);
}

/* Skips a block comment, which the format doesn't have, after reporting it. */
static void skip_block_comment(struct lexer *lexer) {
    report_error(lexer->diagnostics, position_at(lexer, lexer->offset),
                 "there are no block comments: a comment starts with // and ends with its line");
    size_t offset = lexer->offset + 2;
    while (offset < lexer->size &&
           !(lexer->source[offset] == '*' && byte_at(lexer, offset + 1) == '/')) {
        if (lexer->source[offset] == '\n') {
            lexer->line++;
            lexer->line_start = offset + 1;
        }
        offset++;
    }
    lexer->offset = offset < lexer->size ? offset + 2 : offset;
}

/* Skips what comes before the next token: whitespace and comments, gathering doc comments. Doc
 * comments start with exactly three slashes. */
static void skip_space(struct lexer *lexer) {
    while (lexer->offset < lexer->size) {
        const char c = lexer->source[lexer->offset];
        if (c == '\n') {
            lexer->offset++;
            lexer->line++;
            lexer->line_start = lexer->offset;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            lexer->offset++;
        } else if (c == '/' && byte_at(lexer, lexer->offset + 1) == '/') {
            if (byte_at(lexer, lexer->offset + 2) == '/' &&
                byte_at(lexer, lexer->offset + 3) != '/') {
                add_doc_line(lexer);
            } else {
                skip_comment(lexer);
            }
        } else if (c == '/' && byte_at(lexer, lexer->offset + 1) == '*') {
            skip_block_comment(lexer);
        } else {
            return;
        }
    }
}

/* Whether `c` can be part of a word: a name, a keyword or a number. Non-ASCII is, so that a word
 * holding some stays one word. */
static bool is_word_byte(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
        return true;
    }
    return c >= 0x80;
}

/* The kind of the word at the token's text. */
static enum token_kind word_kind(const struct token *token) {
    if (token->text[0] >= '0' && token->text[0] <= '9') {
        return TOKEN_NUMBER;
    }
    for (enum token_kind kind = TOKEN_FN; kind <= TOKEN_USES; kind++) {
        if (strlen(spellings[kind]) == token->length &&
            memcmp(spellings[kind], token->text, token->length) == 0) {
            return kind;
        }
    }
    return TOKEN_NAME;
}

/* Lexes the word at the token's start. Returns false if it holds nothing but non-ASCII, which it
 * reports and skips. */
static bool lex_word(struct lexer *lexer, struct token *token) {
    size_t non_ascii = lexer->size;
    bool ascii = false;
    while (lexer->offset < lexer->size && is_word_byte(byte_at(lexer, lexer->offset))) {
        if (byte_at(lexer, lexer->offset) >= 0x80) {
            if (non_ascii == lexer->size) {
                non_ascii = lexer->offset;
            }
        } else {
            ascii = true;
        }
        lexer->offset++;
    }
    if (non_ascii != lexer->size) {
        report_error(lexer->diagnostics, position_at(lexer, non_ascii),
                     "non-ASCII characters are only allowed in comments");
        token->reported = true;
    }
    token->length = (size_t)(lexer->source + lexer->offset - token->text);
    token->kind = word_kind(token);
    return ascii;
}

/* Lexes the punctuation at the token's start, the longest that matches, as `::` rather than
 * `:`. Returns false if there's none, having reported and skipped the character there. */
static bool lex_punctuation(struct lexer *lexer, struct token *token) {
    const size_t left = lexer->size - lexer->offset;
    for (enum token_kind kind = TOKEN_LEFT_BRACE; kind <= TOKEN_PLUS; kind++) {
        const size_t length = strlen(spellings[kind]);
        if (length <= left && length > token->length &&
            memcmp(spellings[kind], token->text, length) == 0) {
            token->kind = kind;
            token->length = length;
        }
    }
    if (token->length > 0) {
        lexer->offset += token->length;
        return true;
    }
    const unsigned char c = byte_at(lexer, lexer->offset);
    if (c > ' ' && c < 0x7f) {
        report_error(lexer->diagnostics, token->position, "unexpected character `%c`", c);
    } else {
        report_error(lexer->diagnostics, token->position, "unexpected control character 0x%02x",
                     (unsigned int)c);
    }
    lexer->offset++;
    return false;
}

struct token lexer_next(struct lexer *lexer) {
    struct token token;
    for (;;) {
        skip_space(lexer);
        memset(&token, 0, sizeof token);
        token.text = lexer->source + lexer->offset;
        token.position = position_at(lexer, lexer->offset);
        if (lexer->offset == lexer->size) {
            token.kind = TOKEN_END;
            break;
        }
        if (is_word_byte(byte_at(lexer, lexer->offset))) {
            if (lex_word(lexer, &token)) {
                break;
            }
        } else if (lex_punctuation(lexer, &token)) {
            break;
        }
    }
    token.doc.lines = lexer->doc_lines;
    token.doc.count = lexer->doc_count;
    token.doc_position = lexer->doc_position;
    lexer->doc_lines = NULL;
    lexer->doc_count = 0;
    lexer->doc_capacity = 0;
    return token;
}
