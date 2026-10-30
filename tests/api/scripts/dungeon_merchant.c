/* A dungeon script, as dungeon_npc.c describes them, that provides the optional entry point
 * `describe` through its macro, and leaves out `on_hit`. It describes itself with a string literal
 * until its first tick, and from then on with the name the host last gave it, which it keeps. */

#include "dungeon_npc.h"
#include "example_dungeon_npc.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

const char my_name[] = "merchant";

/* The name the host gave the script in its last tick. */
static example_dungeon_string description;

void example_dungeon_npc_tick(uint64_t frame) {
    play(frame);
    free(description.data);
    description = example_dungeon_world_entity_name(me);
}

EXAMPLE_DUNGEON_NPC_DESCRIBE(void) {
    if (description.data == NULL) {
        return example_dungeon_string_of("a merchant");
    }
    return description;
}
