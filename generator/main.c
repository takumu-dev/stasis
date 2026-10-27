/* stasis-gen, Stasis's generator: it reads an interface file and checks it, prints what a host
 * developer needs from it, or writes the host's side of it or its WIT rendering.
 *
 * It exits with 0 when it did what it was asked, 1 when the interface has errors, and 2 when it
 * couldn't run: for a bad command line, a file it can't read or write, or a lack of memory. It
 * writes nothing when the interface has errors. */

#include "arena.h"
#include "c_names.h"
#include "diagnostics.h"
#include "files.h"
#include "host.h"
#include "interface.h"
#include "signatures.h"
#include "wit.h"

#include <errno.h>
#include <stdbool.h>
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
    "usage: stasis-gen <command> [<option>...] <interface file>\n"
    "       stasis-gen --version\n"
    "commands:\n"
    "  check       check the interface file, and write nothing\n"
    "  host        write the host's side of the interface, as a C header and source\n"
    "  signatures  print the signature encoding and hash of each host function, method and entry\n"
    "              point\n"
    "  wit         write the interface's WIT rendering\n"
    "options of host:\n"
    "  --prefix <prefix>         start every C name with <prefix>, not <namespace>_<name>_\n"
    "options of host and wit:\n"
    "  --output-directory <dir>  write into <dir>, not the current directory\n";

/* What a command is given besides its interface file. */
struct options {
    /* The interface file. */
    const char *path;
    /* What starts every C name, which is the interface's default unless one is given. */
    const char *prefix;
    /* Where the command writes its files, or null for the current directory. */
    const char *output_directory;
};

/* A command, which runs on an interface read with no problems, and returns the exit status. */
typedef int command_function(const struct interface *interface, const struct options *options,
                             struct arena *arena);

/* Does nothing more, since reading the interface has checked it. */
static int check(const struct interface *interface, const struct options *options,
                 struct arena *arena) {
    (void)interface;
    (void)options;
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
static int signatures(const struct interface *interface, const struct options *options,
                      struct arena *arena) {
    (void)options;
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

/* The last part of a path: the name of the file it leads to. */
static const char *base_name(const char *path) {
    const char *name = path;
    for (const char *c = path; *c != '\0'; c++) {
        if (*c == '/' || *c == '\\') {
            name = c + 1;
        }
    }
    return name;
}

/* Writes each of `count` files into the output directory, leaving alone any that holds what it
 * would write already. */
static int write_files(const struct output_file *files, size_t count, const struct options *options,
                       struct arena *arena) {
    for (size_t i = 0; i < count; i++) {
        const char *path = files[i].name;
        if (options->output_directory != NULL) {
            path = arena_format(arena, "%s/%s", options->output_directory, files[i].name);
        }
        if (!update_file(path, files[i].contents, files[i].size)) {
            /* NOLINTNEXTLINE(concurrency-mt-unsafe): stasis-gen runs on one thread */
            fprintf(stderr, "stasis-gen: can't write %s: %s\n", path, strerror(errno));
            return EXIT_USAGE;
        }
    }
    return EXIT_SUCCESS;
}

/* Writes the host's side of the interface. */
static int host(const struct interface *interface, const struct options *options,
                struct arena *arena) {
    struct output_file files[HOST_FILE_COUNT];
    generate_host(interface, options->prefix, base_name(options->path), arena, files);
    return write_files(files, HOST_FILE_COUNT, options, arena);
}

/* Writes the interface's WIT rendering. */
static int wit(const struct interface *interface, const struct options *options,
               struct arena *arena) {
    struct output_file file;
    generate_wit(interface, base_name(options->path), arena, &file);
    return write_files(&file, 1, options, arena);
}

/* The commands, by name, with the options each takes. */
static const struct {
    const char *name;
    command_function *run;
    bool takes_prefix;
    bool takes_output_directory;
} commands[] = {
    {"check", check, false, false},
    {"host", host, true, true},
    {"signatures", signatures, false, false},
    {"wit", wit, false, true},
};

/* Runs `command` on the interface file that `options` names. */
static int run(command_function *command, struct options *options) {
    size_t size = 0;
    char *source = read_file(options->path, &size);
    if (source == NULL) {
        /* NOLINTNEXTLINE(concurrency-mt-unsafe): stasis-gen runs on one thread */
        fprintf(stderr, "stasis-gen: can't read %s: %s\n", options->path, strerror(errno));
        return EXIT_USAGE;
    }
    struct arena arena = {NULL};
    struct diagnostics diagnostics = {NULL, NULL, NULL, 0};
    diagnostics.arena = &arena;
    const struct interface *interface = read_interface(source, size, &arena, &diagnostics);
    if (options->prefix == NULL) {
        options->prefix = default_prefix(interface, &arena);
    }
    check_c_names(interface, options->prefix, &arena, &diagnostics);
    check_wit_names(interface, &arena, &diagnostics);
    int status = EXIT_ERRORS;
    if (diagnostics.count == 0) {
        status = command(interface, options, &arena);
    } else {
        print_diagnostics(&diagnostics, options->path, stderr);
    }
    arena_free(&arena);
    free(source);
    return status;
}

/* Reads the arguments after the command, which is commands[command], into `options`. Returns
 * false if they aren't what the command takes, having said why unless the usage says enough. */
static bool read_arguments(int argc, char **argv, size_t command, struct options *options) {
    for (int i = 2; i < argc; i++) {
        const char *argument = argv[i];
        const char **value = NULL;
        bool taken = false;
        if (strcmp(argument, "--prefix") == 0) {
            value = &options->prefix;
            taken = commands[command].takes_prefix;
        } else if (strcmp(argument, "--output-directory") == 0) {
            value = &options->output_directory;
            taken = commands[command].takes_output_directory;
        } else if (strncmp(argument, "--", 2) == 0) {
            fprintf(stderr, "stasis-gen: there's no option `%s`\n", argument);
            return false;
        } else if (options->path == NULL) {
            options->path = argument;
            continue;
        } else {
            return false;
        }
        if (!taken) {
            fprintf(stderr, "stasis-gen: `%s` takes no `%s`\n", commands[command].name, argument);
            return false;
        }
        if (*value != NULL) {
            fprintf(stderr, "stasis-gen: `%s` is given twice\n", argument);
            return false;
        }
        if (i + 1 == argc) {
            fprintf(stderr, "stasis-gen: `%s` needs a value\n", argument);
            return false;
        }
        i++;
        *value = argv[i];
    }
    return options->path != NULL;
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
    size_t command = 0;
    while (argc > 1 && command < sizeof commands / sizeof commands[0] &&
           strcmp(argv[1], commands[command].name) != 0) {
        command++;
    }
    if (argc > 1 && command == sizeof commands / sizeof commands[0]) {
        fprintf(stderr, "stasis-gen: there's no command `%s`\n", argv[1]);
        fputs(usage, stderr);
        return EXIT_USAGE;
    }
    struct options options = {NULL, NULL, NULL};
    if (argc < 3 || !read_arguments(argc, argv, command, &options)) {
        fputs(usage, stderr);
        return EXIT_USAGE;
    }
    const char *problem = options.prefix == NULL ? NULL : prefix_problem(options.prefix);
    if (problem != NULL) {
        fprintf(stderr, "stasis-gen: the prefix `%s` %s\n", options.prefix, problem);
        return EXIT_USAGE;
    }
    return run(commands[command].run, &options);
}
