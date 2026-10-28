#include "message.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

void stasis_message_init(struct message *message, char *buffer, size_t size) {
    message->buffer = buffer;
    message->size = size;
    message->length = 0;
    message->lines = 0;
    message->total = 0;
    if (size > 0) {
        buffer[0] = '\0';
    }
}

/* Where the next text goes, or null when there's no buffer. */
static char *tail(const struct message *message) {
    return message->size == 0 ? NULL : message->buffer + message->length;
}

/* Moves past `written` more bytes, of which the buffer holds what fits before its last byte. */
static void advance(struct message *message, int written) {
    if (written <= 0) {
        return;
    }
    message->total += (size_t)written;
    if (message->size == 0) {
        return;
    }
    size_t added = (size_t)written;
    if (added > message->size - 1 - message->length) {
        added = message->size - 1 - message->length;
    }
    message->length += added;
}

void stasis_message_add_v(struct message *message, const char *format, va_list arguments) {
    message->lines++;
    if (message->lines > 1) {
        advance(message, snprintf(tail(message), message->size - message->length, "\n"));
    }
    advance(message, vsnprintf(tail(message), message->size - message->length, format, arguments));
}

void stasis_message_add(struct message *message, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    stasis_message_add_v(message, format, arguments);
    va_end(arguments);
}
