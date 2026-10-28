#include "names.h"

#include <stasis.h>

#include "signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Where parsing a name has got to, and where the name ends. */
struct cursor {
    const char *at;
    const char *end;
};

/* Whether `c` is a lowercase ASCII letter. */
static bool is_lower(char c) {
    if (c < 'a') {
        return false;
    }
    return c <= 'z';
}

/* Whether `c` is an ASCII digit. */
static bool is_digit(char c) {
    if (c < '0') {
        return false;
    }
    return c <= '9';
}

/* Takes `c` if it comes next. */
static bool take(struct cursor *cursor, char c) {
    if (cursor->at == cursor->end || *cursor->at != c) {
        return false;
    }
    cursor->at++;
    return true;
}

/* Parses a snake_case name: words of lowercase letters and digits, each starting with a letter,
 * joined by `_`. */
static bool parse_snake_case(struct cursor *cursor, stasis_string *name) {
    const char *start = cursor->at;
    do {
        if (cursor->at == cursor->end || !is_lower(*cursor->at)) {
            return false;
        }
        while (cursor->at != cursor->end && (is_lower(*cursor->at) || is_digit(*cursor->at))) {
            cursor->at++;
        }
    } while (take(cursor, '_'));
    name->data = start;
    name->size = (size_t)(cursor->at - start);
    return true;
}

/* Parses a decimal number of 32 bits, with no leading zero unless it's 0. */
static bool parse_number(struct cursor *cursor, uint32_t *number) {
    if (cursor->at == cursor->end || !is_digit(*cursor->at)) {
        return false;
    }
    if (*cursor->at == '0') {
        cursor->at++;
        *number = 0;
        if (cursor->at != cursor->end && is_digit(*cursor->at)) {
            return false;
        }
        return true;
    }
    uint32_t value = 0;
    while (cursor->at != cursor->end && is_digit(*cursor->at)) {
        const uint32_t digit = (uint32_t)(*cursor->at - '0');
        if (value > (UINT32_MAX - digit) / 10) {
            return false;
        }
        value = (value * 10) + digit;
        cursor->at++;
    }
    *number = value;
    return true;
}

/* Parses a line: a major version of at least 1, or `0.` and a minor version. */
static bool parse_line(struct cursor *cursor, struct line *line) {
    if (!parse_number(cursor, &line->major)) {
        return false;
    }
    line->minor = 0;
    if (line->major > 0) {
        return true;
    }
    if (!take(cursor, '.')) {
        return false;
    }
    return parse_number(cursor, &line->minor);
}

/* Parses what every name of the interface starts with: `<namespace>:<name>/<scope>@<line>`. */
static bool parse_interface(struct cursor *cursor, struct wire_name *name) {
    if (!parse_snake_case(cursor, &name->space) || !take(cursor, ':') ||
        !parse_snake_case(cursor, &name->name) || !take(cursor, '/') ||
        !parse_snake_case(cursor, &name->scope) || !take(cursor, '@')) {
        return false;
    }
    return parse_line(cursor, &name->line);
}

/* Parses a hash: 16 lowercase hexadecimal digits. */
static bool parse_hash(struct cursor *cursor, stasis_string *hash) {
    if ((size_t)(cursor->end - cursor->at) < STASIS_SIGNATURE_HASH_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < STASIS_SIGNATURE_HASH_LENGTH; i++) {
        const char c = cursor->at[i];
        if (!is_digit(c) && !(c >= 'a' && c <= 'f')) {
            return false;
        }
    }
    hash->data = cursor->at;
    hash->size = STASIS_SIGNATURE_HASH_LENGTH;
    cursor->at += STASIS_SIGNATURE_HASH_LENGTH;
    return true;
}

/* Starts parsing `text`, with `name`'s parts empty. */
static struct cursor start(stasis_string text, struct wire_name *name) {
    memset(name, 0, sizeof *name);
    struct cursor cursor;
    cursor.at = text.data;
    cursor.end = text.data + text.size;
    return cursor;
}

/* Whether `text` holds the character `c`. */
static bool contains(stasis_string text, char c) {
    if (text.size == 0) {
        return false;
    }
    return memchr(text.data, c, text.size) != NULL;
}

bool stasis_is_entry_point_name(stasis_string text) {
    if (contains(text, ':')) {
        return true;
    }
    return contains(text, '#');
}

bool stasis_is_group_name(stasis_string text) { return contains(text, ':'); }

bool stasis_parse_entry_point_name(stasis_string text, struct wire_name *name) {
    struct cursor cursor = start(text, name);
    if (text.size == 0) {
        return false;
    }
    if (!parse_interface(&cursor, name) || !take(&cursor, '#') ||
        !parse_snake_case(&cursor, &name->entry_point) || !take(&cursor, ':') ||
        !parse_hash(&cursor, &name->hash)) {
        return false;
    }
    return cursor.at == cursor.end;
}

bool stasis_parse_group_name(stasis_string text, struct wire_name *name) {
    struct cursor cursor = start(text, name);
    if (text.size == 0 || !parse_interface(&cursor, name)) {
        return false;
    }
    return cursor.at == cursor.end;
}

bool stasis_string_is(stasis_string text, const char *string) {
    const size_t length = strlen(string);
    if (text.size != length) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    return memcmp(text.data, string, length) == 0;
}
