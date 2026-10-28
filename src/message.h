/* A message of a line for each problem, NUL-terminated and cut short where its buffer ends, as
 * WAMR's error buffers are: the one loading writes into the host's buffer, and the reason a script
 * isn't snapshottable. A message written into no buffer at all measures how long it would be. */

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
    /* The length of the whole message, however much of it the buffer holds. */
    size_t total;
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
