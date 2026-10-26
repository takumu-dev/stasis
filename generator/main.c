/* stasis-gen, Stasis's generator: it reads an interface file and checks it, or prints what a host
 * developer needs from it.
 *
 * It exits with 0 when it did what it was asked, 1 when the interface has errors, and 2 when it
 * couldn't run: for a bad command line, an interface file it can't read, or a lack of memory. */

#include "arena.h"
#include "diagnostics.h"
#include "interface.h"
#include "signatures.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The exit status for errors in the interface. */
enum { EXIT_ERRORS = 1 };

/* The exit status for anything that stops stasis-gen from running. */
enum { EXIT_USAGE = 2 };

/* What --help prints, and what a bad command line gets. */
static const char usage[] =
    "usage: stasis-gen <command> <interface file>\n"
    "       stasis-gen --version\n"
    "commands:\n"
    "  check       check the interface file, and write nothing\n"
    "  signatures  print the signature encoding and hash of each host function, method and entry\n"
    "              point\n";

/* Reads a whole file, as a string the caller frees. It sets `size` to the file's size, which
 * doesn't count the terminating null. Returns null if the file can't be read, leaving errno to say
 * why. */
static char *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    size_t used = 0;
    size_t capacity = 1 << 12;
    char *text = malloc(capacity);
    while (text != NULL) {
        used += fread(text + used, 1, capacity - used - 1, file);
        if (used < capacity - 1) {
            break;
        }
        capacity *= 2;
        char *grown = realloc(text, capacity);
        if (grown == NULL) {
            free(text);
        }
        text = grown;
    }
    if (text != NULL && ferror(file) != 0) {
        free(text);
        text = NULL;
    }
    const int error = errno;
    fclose(file);
    errno = error;
    if (text != NULL) {
        text[used] = '\0';
        *size = used;
    }
    return text;
}

/* A command, which runs on an interface read with no problems, and returns the exit status. */
typedef int command_function(const struct interface *interface, struct arena *arena);

/* Does nothing more, since reading the interface has checked it. */
static int check(const struct interface *interface, struct arena *arena) {
    (void)interface;
    (void)arena;
    return EXIT_SUCCESS;
}

/* Prints a function's qualified name, the encoding of its signature, and its hash. */
static void print_signature(const char *scope, const struct function *function,
                            struct arena *arena) {
    struct signature signature;
    encode_signature(function, arena, &signature);
    printf("%s.%s %s %s\n", scope, function->name.text, signature.encoding, signature.hash);
}

/* Prints the signature of each host function, method and entry point, in the order the file
 * declares them, with the kind of script's entry points last. */
static int signatures(const struct interface *interface, struct arena *arena) {
    for (const struct group *group = interface->groups; group != NULL; group = group->next) {
        for (const struct item *item = group->items; item != NULL; item = item->next) {
            if (item->kind == ITEM_FUNCTION) {
                print_signature(group->name.text, item->as.function, arena);
            } else if (item->kind == ITEM_OBJECT) {
                const struct object *object = item->as.object;
                const char *scope =
                    arena_format(arena, "%s.%s", group->name.text, object->name.text);
                for (const struct function *method = object->methods; method != NULL;
                     method = method->next) {
                    print_signature(scope, method, arena);
                }
            }
        }
    }
    for (const struct script *script = interface->scripts; script != NULL; script = script->next) {
        for (const struct function *entry_point = script->entry_points; entry_point != NULL;
             entry_point = entry_point->next) {
            print_signature(script->name.text, entry_point, arena);
        }
    }
    return EXIT_SUCCESS;
}

/* The commands, by name. */
static const struct {
    const char *name;
    command_function *run;
} commands[] = {
    {"check", check},
    {"signatures", signatures},
};

/* Runs the command `name` on the interface file at `path`. */
static int run(const char *name, const char *path) {
    command_function *command = NULL;
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++) {
        if (strcmp(name, commands[i].name) == 0) {
            command = commands[i].run;
        }
    }
    if (command == NULL) {
        fprintf(stderr, "stasis-gen: there's no command `%s`\n%s", name, usage);
        return EXIT_USAGE;
    }

    size_t size = 0;
    char *source = read_file(path, &size);
    if (source == NULL) {
        /* NOLINTNEXTLINE(concurrency-mt-unsafe): stasis-gen runs on one thread */
        fprintf(stderr, "stasis-gen: can't read %s: %s\n", path, strerror(errno));
        return EXIT_USAGE;
    }
    struct arena arena = {NULL};
    struct diagnostics diagnostics = {NULL, NULL, NULL, 0};
    diagnostics.arena = &arena;
    const struct interface *interface = read_interface(source, size, &arena, &diagnostics);
    int status = EXIT_ERRORS;
    if (diagnostics.count == 0) {
        status = command(interface, &arena);
    } else {
        print_diagnostics(&diagnostics, path, stderr);
    }
    arena_free(&arena);
    free(source);
    return status;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("stasis-gen %s\n", STASIS_VERSION);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        fputs(usage, stdout);
        return EXIT_SUCCESS;
    }
    if (argc == 3) {
        return run(argv[1], argv[2]);
    }
    fputs(usage, stderr);
    return EXIT_USAGE;
}
