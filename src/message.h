/* The message that loading writes into the host's buffer: a NUL-terminated line for each problem,
 * cut short where the buffer ends, as WAMR's error buffers are. */

#ifndef STASIS_MESSAGE_H
#define STASIS_MESSAGE_H

#include <stdarg.h>
#include <stddef.h>

/* Marks a function whose parameter number `string` is a printf format, with its arguments from
 * parameter number `first` on, or none for 0. */
#ifdef __GNUC__
#define STASIS_PRINTF_FORMAT(string, first) __attribute__((format(printf, string, first)))
#else
#define STASIS_PRINTF_FORMAT(string, first)
#endif

/* A message being written. */
struct message {
    /* The host's buffer, which may be null when `size` is 0. */
    char *buffer;
    size_t size;
    /* The length of what the buffer holds, which is less than `size` unless `size` is 0. */
    size_t length;
    /* The number of lines written, including any cut off. */
    size_t lines;
};

/* Starts a message in `buffer`, which holds `size` bytes, and writes it empty. */
void stasis_message_init(struct message *message, char *buffer, size_t size);

/* Adds a line, formatted as by snprintf. */
void stasis_message_add(struct message *message, const char *format, ...)
    STASIS_PRINTF_FORMAT(2, 3);

/* Adds a line, formatted as by vsnprintf. */
void stasis_message_add_v(struct message *message, const char *format, va_list arguments)
    STASIS_PRINTF_FORMAT(2, 0);

#endif
