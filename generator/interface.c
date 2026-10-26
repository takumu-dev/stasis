#include "interface.h"

#include "arena.h"
#include "checker.h"
#include "diagnostics.h"
#include "parser.h"

#include <stddef.h>

struct interface *read_interface(const char *source, size_t size, struct arena *arena,
                                 struct diagnostics *diagnostics) {
    struct interface *interface = parse_interface(source, size, arena, diagnostics);
    check_interface(interface, arena, diagnostics);
    return interface;
}
