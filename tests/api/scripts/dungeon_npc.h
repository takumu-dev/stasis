/* What the dungeon scripts share, which dungeon_npc.c defines. */

#ifndef STASIS_TESTS_API_SCRIPTS_DUNGEON_NPC_H
#define STASIS_TESTS_API_SCRIPTS_DUNGEON_NPC_H

#include "example_dungeon_npc.h"

#include <stdint.h>

/* The name of the script's own entity, which each script defines. */
extern const char my_name[];

/* The script's own entity, which it finds as it starts. */
extern example_dungeon_world_entity me;

/* Calls every host function and method, as the script's tick does, and prints what each returns. */
void play(uint64_t frame);

#endif
