/* What the dungeon scripts share. They're C scripts built against the C bindings of dungeon.stasis
 * alone, as a script author builds a script from a modding kit, and each defines my_name and its
 * entry points. They start by finding their own entity, in a constructor, and play() calls every
 * host function and method, so that every type crosses both ways: it passes the host the values
 * the host gave it, or values worked out from them, and prints what it's given.
 *
 * Strings that the host returns are the script's, so play() frees them, and the script's own
 * strings stay its own when it passes them to a host function, so it passes the host both a string
 * literal and a string the host returned. If the bindings freed either, the C library would
 * trap. */

#include "dungeon_npc.h"

#include "example_dungeon_npc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

example_dungeon_world_entity me;

/* Runs once, when Stasis starts a new instance, and calls a host function, as start-up code may. */
__attribute__((constructor)) static void find_me(void) {
    me = example_dungeon_world_find(example_dungeon_string_of(my_name));
    printf("started as %u\n", me.handle);
}

void play(uint64_t frame) {
    printf("frame %llu\n", (unsigned long long)frame);

    example_dungeon_string name = example_dungeon_world_entity_name(me);
    const example_dungeon_world_entity found = example_dungeon_world_find(name);
    printf("name of %zu bytes, which finds %s\n", name.size,
           found.handle == me.handle ? "me" : "another");
    free(name.data);

    const example_dungeon_world_vec2 position = example_dungeon_world_entity_position(me);
    const float light = example_dungeon_world_light(position);
    const bool alive = example_dungeon_world_entity_alive(me);
    const uint8_t level = example_dungeon_world_entity_level(me);
    const int8_t mood = example_dungeon_world_entity_cheer(me, -100);
    const int16_t health = example_dungeon_world_entity_heal(me, -30000);
    const double walked = example_dungeon_world_entity_walk(me, position, 0.75F);
    printf("at %a %a, light %a, alive %d, level %u, mood %d, health %d, walked %a\n",
           (double)position.x, (double)position.y, (double)light, alive, level, mood, health,
           walked);

    const example_dungeon_world_entity other = example_dungeon_world_nearest(me, 2.5);
    const bool attacked = example_dungeon_combat_attack(other, 4000000000U);
    example_dungeon_combat_hit hit = example_dungeon_combat_last_hit(other);
    const uint32_t heard = example_dungeon_combat_alarm(hit);
    printf("nearest %u, attacked %d, last hit by %u with %.*s for %u at %a %a, heard by %u\n",
           other.handle, attacked, hit.by.handle, (int)hit.weapon.size, hit.weapon.data, hit.damage,
           (double)hit.at.x, (double)hit.at.y, heard);
    example_dungeon_combat_hit_free(&hit);

    const uint16_t stock = example_dungeon_trade_stock(65535);
    const uint32_t price = example_dungeon_trade_price(stock, level);
    const example_dungeon_trade_offer offer = {stock, level, price, -2000000000, frame + 1};
    const int32_t profit = example_dungeon_trade_sell(other, offer);
    const int64_t purse = example_dungeon_trade_pay(other, -9000000000);
    printf("stock %u, price %u, profit %d, purse %lld\n", stock, price, profit, (long long)purse);
}
