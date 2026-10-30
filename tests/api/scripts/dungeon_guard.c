/* A dungeon script, as dungeon_npc.c describes them, that provides the optional entry point
 * `on_hit` through its macro, and leaves out `describe`. */

#include "dungeon_npc.h"
#include "example_dungeon_npc.h"

#include <stdint.h>
#include <stdio.h>

const char my_name[] = "guard";

void example_dungeon_npc_tick(uint64_t frame) { play(frame); }

/* Prints the hit, with no more than the first 12 bytes of the weapon, and says whether it
 * hurts. */
EXAMPLE_DUNGEON_NPC_ON_HIT(example_dungeon_combat_hit blow) {
    printf("hit by %u with %.*s (%zu bytes) for %u at %a %a\n", blow.by.handle,
           blow.weapon.size < 12 ? (int)blow.weapon.size : 12, blow.weapon.data, blow.weapon.size,
           blow.damage, (double)blow.at.x, (double)blow.at.y);
    return blow.damage > 10;
}
