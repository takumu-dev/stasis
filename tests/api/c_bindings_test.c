/* C scripts built against the C bindings of dungeon.stasis alone, as dungeon_npc.c describes them:
 * what they export and carry, how they start, every type crossing both ways through the bindings,
 * and who frees the strings that cross. The scripts' memory shows what they free: it grows by
 * pages, so a string of a few pages that one call left behind would grow it on every call. */

#include <stasis.h>

#include "example_dungeon.h"
#include "harness.h"
#include "sha256.h"
#include "support.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A host object of the kind world.entity. */
struct entity {
    /* How the host's log names it. */
    const char *id;
    /* The name it gives scripts, which is its id unless a test makes it long. */
    const char *name;
    size_t name_size;
};

static struct entity guard = {"guard", "guard", 5};
static struct entity merchant = {"merchant", "merchant", 8};
static struct entity rat = {"rat", "rat", 3};

static struct entity *const entities[] = {&guard, &merchant, &rat};

/* A name of many pages, which a script that kept it, or a copy of it, would grow its memory by on
 * every call. */
static char long_name[200000];

/* The host object that an opaque pointer from the host's side points at. */
static struct entity *entity_of(example_dungeon_world_entity *object) {
    return (struct entity *)(void *)object;
}

/* The opaque pointer to a host object, as the host's side passes it. */
static example_dungeon_world_entity *object_of(struct entity *entity) {
    return (example_dungeon_world_entity *)(void *)entity;
}

/* The host functions called, in order, each with the entity it's called on or returns, if any,
 * separated by spaces. */
static char calls[4096];
static size_t calls_size;

/* Logs a call of the host function `name`, with the entity it's called on or returns, if any. */
static void log_call(const char *name, const struct entity *entity) {
    const int length = snprintf(calls + calls_size, sizeof calls - calls_size, "%s%s%s%s%s",
                                calls_size > 0 ? " " : "", name, entity != NULL ? "(" : "",
                                entity != NULL ? entity->id : "", entity != NULL ? ")" : "");
    ASSERT_TRUE(length >= 0 && (size_t)length < sizeof calls - calls_size);
    calls_size += (size_t)length;
}

/* What the host functions were last passed, beside the entities they're called on. */
static struct {
    int8_t cheer_by;
    int16_t heal_amount;
    example_dungeon_world_vec2 walk_to;
    float walk_speed;
    double nearest_radius;
    example_dungeon_world_vec2 light_at;
    uint32_t attack_damage;
    struct entity *alarm_by;
    char alarm_weapon[16];
    uint32_t alarm_damage;
    example_dungeon_world_vec2 alarm_at;
    uint16_t stock_item;
    uint16_t price_item;
    uint8_t price_quantity;
    example_dungeon_trade_offer sell_terms;
    int64_t pay_amount;
} passed;

stasis_string example_dungeon_world_entity_name(stasis_instance *instance,
                                                example_dungeon_world_entity *self) {
    (void)instance;
    log_call("world.entity.name", entity_of(self));
    const stasis_string name = {entity_of(self)->name, entity_of(self)->name_size};
    return name;
}

example_dungeon_world_vec2
example_dungeon_world_entity_position(stasis_instance *instance,
                                      example_dungeon_world_entity *self) {
    (void)instance;
    log_call("world.entity.position", entity_of(self));
    const example_dungeon_world_vec2 position = {-1.5F, 2.25F};
    return position;
}

bool example_dungeon_world_entity_alive(stasis_instance *instance,
                                        example_dungeon_world_entity *self) {
    (void)instance;
    log_call("world.entity.alive", entity_of(self));
    return true;
}

uint8_t example_dungeon_world_entity_level(stasis_instance *instance,
                                           example_dungeon_world_entity *self) {
    (void)instance;
    log_call("world.entity.level", entity_of(self));
    return 200;
}

int8_t example_dungeon_world_entity_cheer(stasis_instance *instance,
                                          example_dungeon_world_entity *self, int8_t by) {
    (void)instance;
    log_call("world.entity.cheer", entity_of(self));
    passed.cheer_by = by;
    return INT8_MIN;
}

int16_t example_dungeon_world_entity_heal(stasis_instance *instance,
                                          example_dungeon_world_entity *self, int16_t amount) {
    (void)instance;
    log_call("world.entity.heal", entity_of(self));
    passed.heal_amount = amount;
    return INT16_MIN;
}

double example_dungeon_world_entity_walk(stasis_instance *instance,
                                         example_dungeon_world_entity *self,
                                         example_dungeon_world_vec2 to, float speed) {
    (void)instance;
    log_call("world.entity.walk", entity_of(self));
    passed.walk_to = to;
    passed.walk_speed = speed;
    return 9.140625;
}

example_dungeon_world_entity *example_dungeon_world_find(stasis_instance *instance,
                                                         stasis_string name) {
    for (size_t i = 0; i < sizeof entities / sizeof entities[0]; i++) {
        struct entity *entity = entities[i];
        if (name.size == entity->name_size && memcmp(name.data, entity->name, name.size) == 0) {
            log_call("world.find", entity);
            return object_of(entity);
        }
    }
    ASSERT_INT_EQ(stasis_abort(instance, "no entity has that name"), STASIS_OK);
    return NULL;
}

example_dungeon_world_entity *example_dungeon_world_nearest(stasis_instance *instance,
                                                            example_dungeon_world_entity *from,
                                                            double radius) {
    (void)instance;
    log_call("world.nearest", entity_of(from));
    passed.nearest_radius = radius;
    return object_of(&rat);
}

float example_dungeon_world_light(stasis_instance *instance, example_dungeon_world_vec2 at) {
    (void)instance;
    log_call("world.light", NULL);
    passed.light_at = at;
    return 0.328125F;
}

bool example_dungeon_combat_attack(stasis_instance *instance, example_dungeon_world_entity *target,
                                   uint32_t damage) {
    (void)instance;
    log_call("combat.attack", entity_of(target));
    passed.attack_damage = damage;
    return true;
}

example_dungeon_combat_hit example_dungeon_combat_last_hit(stasis_instance *instance,
                                                           example_dungeon_world_entity *of) {
    (void)instance;
    log_call("combat.last_hit", entity_of(of));
    example_dungeon_combat_hit hit;
    hit.by = object_of(&guard);
    hit.weapon.data = "rusty sword";
    hit.weapon.size = strlen(hit.weapon.data);
    hit.damage = 3000000000U;
    hit.at.x = 0.5F;
    hit.at.y = -0.25F;
    return hit;
}

uint32_t example_dungeon_combat_alarm(stasis_instance *instance, example_dungeon_combat_hit about) {
    (void)instance;
    log_call("combat.alarm", NULL);
    passed.alarm_by = entity_of(about.by);
    ASSERT_TRUE(about.weapon.size < sizeof passed.alarm_weapon);
    memcpy(passed.alarm_weapon, about.weapon.data, about.weapon.size);
    passed.alarm_weapon[about.weapon.size] = '\0';
    passed.alarm_damage = about.damage;
    passed.alarm_at = about.at;
    return UINT32_MAX;
}

uint16_t example_dungeon_trade_stock(stasis_instance *instance, uint16_t item) {
    (void)instance;
    log_call("trade.stock", NULL);
    passed.stock_item = item;
    return 65534;
}

uint32_t example_dungeon_trade_price(stasis_instance *instance, uint16_t item, uint8_t quantity) {
    (void)instance;
    log_call("trade.price", NULL);
    passed.price_item = item;
    passed.price_quantity = quantity;
    return 3000000001U;
}

int32_t example_dungeon_trade_sell(stasis_instance *instance, example_dungeon_world_entity *to,
                                   example_dungeon_trade_offer terms) {
    (void)instance;
    log_call("trade.sell", entity_of(to));
    passed.sell_terms = terms;
    return INT32_MIN;
}

int64_t example_dungeon_trade_pay(stasis_instance *instance, example_dungeon_world_entity *to,
                                  int64_t amount) {
    (void)instance;
    log_call("trade.pay", entity_of(to));
    passed.pay_amount = amount;
    return INT64_MIN;
}

/* What the scripts have printed to stdout, up to as much as fits. */
static char output[4096];
static size_t output_size;

static void record_output(stasis_instance *instance, void *user_data, stasis_stream stream,
                          const char *bytes, size_t size) {
    (void)instance;
    (void)user_data;
    ASSERT_INT_EQ(stream, STASIS_STREAM_STDOUT);
    const size_t room = sizeof output - 1 - output_size;
    const size_t kept = size < room ? size : room;
    memcpy(output + output_size, bytes, kept);
    output_size += kept;
    output[output_size] = '\0';
}

/* Forgets the calls, what they were passed and the output, and gives each entity its own name. */
static void forget(void) {
    calls_size = 0;
    calls[0] = '\0';
    memset(&passed, 0, sizeof passed);
    output_size = 0;
    output[0] = '\0';
    for (size_t i = 0; i < sizeof entities / sizeof entities[0]; i++) {
        entities[i]->name = entities[i]->id;
        entities[i]->name_size = strlen(entities[i]->id);
    }
}

/* Gives `entity` the long name, made of `letter`s. */
static void give_long_name(struct entity *entity, char letter) {
    memset(long_name, letter, sizeof long_name);
    entity->name = long_name;
    entity->name_size = sizeof long_name;
}

/* A dungeon script loaded into a library of its own, which sends the script's output to
 * record_output, and an instance of it, which isn't started unless start() starts it. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

static struct running create(const char *name) {
    forget();
    stasis_library_settings settings;
    memset(&settings, 0, sizeof settings);
    settings.output = record_output;
    struct running running;
    ASSERT_INT_EQ(stasis_library_create(&settings, &running.library), STASIS_OK);
    running.script = load_script(running.library, &example_dungeon_interface, name);
    ASSERT_INT_EQ(stasis_instance_create(running.script, NULL, &running.instance), STASIS_OK);
    return running;
}

/* Creates an instance of a dungeon script and starts it, which finds the guard or the merchant,
 * and forgets what that did. */
static struct running start(const char *name) {
    const struct running running = create(name);
    ASSERT_INT_EQ(stasis_instance_start(running.instance), STASIS_OK);
    forget();
    return running;
}

static void stop(struct running running) {
    destroy_all(running.instance, running.script, running.library);
    forget();
}

/* The number of bytes in a snapshot of an instance, which grows with its memory. */
static size_t snapshot_size(stasis_instance *instance) {
    size_t size = 0;
    ASSERT_INT_EQ(stasis_instance_snapshot_size(instance, &size), STASIS_OK);
    return size;
}

/* A frame number whose bits a 32-bit value couldn't hold. */
static const uint64_t frame = 18364758544493064720U;

/* What play() prints, given what the host functions above return, for `frame`. */
static const char played[] =
    "frame 18364758544493064720\n"
    "name of 5 bytes, which finds me\n"
    "at -0x1.8p+0 0x1.2p+1, light 0x1.5p-2, alive 1, level 200, mood -128, health -32768, "
    "walked 0x1.248p+3\n"
    "nearest 2, attacked 1, last hit by 1 with rusty sword for 3000000000 at 0x1p-1 -0x1p-2, "
    "heard by 4294967295\n"
    "stock 65534, price 3000000001, profit -2147483648, purse -9223372036854775808\n";

/* The host functions that play() calls, in order, when the script is the guard. */
static const char played_calls[] =
    "world.entity.name(guard) world.find(guard) world.entity.position(guard) world.light "
    "world.entity.alive(guard) world.entity.level(guard) world.entity.cheer(guard) "
    "world.entity.heal(guard) world.entity.walk(guard) world.nearest(guard) combat.attack(rat) "
    "combat.last_hit(rat) combat.alarm trade.stock trade.price trade.sell(rat) trade.pay(rat)";

/* Every type crosses both ways: each value that a host function returns reaches the script as it
 * was, as the script's output shows, and each value the script passes reaches the host function
 * as it was, as the script passes the host the values it was given, or values of its own. */
TEST(every_type_crosses_both_ways) {
    const struct running running = start("dungeon_guard");
    ASSERT_INT_EQ(example_dungeon_npc_tick(running.instance, frame), STASIS_OK);
    ASSERT_STR_EQ(output, played);
    ASSERT_STR_EQ(calls, played_calls);
    ASSERT_INT_EQ(passed.cheer_by, -100);
    ASSERT_INT_EQ(passed.heal_amount, -30000);
    ASSERT_FLOAT_EQ(passed.walk_to.x, -1.5F);
    ASSERT_FLOAT_EQ(passed.walk_to.y, 2.25F);
    ASSERT_FLOAT_EQ(passed.walk_speed, 0.75F);
    ASSERT_DOUBLE_EQ(passed.nearest_radius, 2.5);
    ASSERT_FLOAT_EQ(passed.light_at.x, -1.5F);
    ASSERT_FLOAT_EQ(passed.light_at.y, 2.25F);
    ASSERT_UINT_EQ(passed.attack_damage, 4000000000U);
    ASSERT_TRUE(passed.alarm_by == &guard);
    ASSERT_STR_EQ(passed.alarm_weapon, "rusty sword");
    ASSERT_UINT_EQ(passed.alarm_damage, 3000000000U);
    ASSERT_FLOAT_EQ(passed.alarm_at.x, 0.5F);
    ASSERT_FLOAT_EQ(passed.alarm_at.y, -0.25F);
    ASSERT_UINT_EQ(passed.stock_item, 65535);
    ASSERT_UINT_EQ(passed.price_item, 65534);
    ASSERT_UINT_EQ(passed.price_quantity, 200);
    ASSERT_UINT_EQ(passed.sell_terms.item, 65534);
    ASSERT_UINT_EQ(passed.sell_terms.quantity, 200);
    ASSERT_UINT_EQ(passed.sell_terms.price, 3000000001U);
    ASSERT_INT_EQ(passed.sell_terms.discount, -2000000000);
    ASSERT_UINT_EQ(passed.sell_terms.until, frame + 1);
    ASSERT_INT_EQ(passed.pay_amount, -9000000000);
    stop(running);
}

/* An entry point takes a record, with a handle, a string, a u32 and a record in it, and returns a
 * bool. */
TEST(an_entry_point_takes_a_record_and_returns_a_bool) {
    const struct running running = start("dungeon_guard");
    example_dungeon_combat_hit blow;
    blow.by = object_of(&rat);
    blow.weapon.data = "axe";
    blow.weapon.size = 3;
    blow.damage = 3000000000U;
    blow.at.x = 0.5F;
    blow.at.y = -0.25F;
    bool hurts = false;
    ASSERT_INT_EQ(example_dungeon_npc_on_hit(running.instance, blow, &hurts), STASIS_OK);
    ASSERT_TRUE(hurts);
    blow.damage = 5;
    ASSERT_INT_EQ(example_dungeon_npc_on_hit(running.instance, blow, &hurts), STASIS_OK);
    ASSERT_TRUE(!hurts);
    ASSERT_STR_EQ(output, "hit by 2 with axe (3 bytes) for 3000000000 at 0x1p-1 -0x1p-2\n"
                          "hit by 2 with axe (3 bytes) for 5 at 0x1p-1 -0x1p-2\n");
    stop(running);
}

/* An entry point's result is copied, so the script can return a string literal, and the string it
 * keeps. */
TEST(an_entry_point_returns_a_string_literal_and_a_string_it_keeps) {
    const struct running running = start("dungeon_merchant");
    stasis_string description = {NULL, 0};
    for (int i = 0; i < 2; i++) {
        ASSERT_INT_EQ(example_dungeon_npc_describe(running.instance, &description), STASIS_OK);
        ASSERT_SIZE_EQ(description.size, strlen("a merchant"));
        ASSERT_MEM_EQ(description.data, "a merchant", description.size);
    }
    ASSERT_INT_EQ(example_dungeon_npc_tick(running.instance, frame), STASIS_OK);
    for (int i = 0; i < 2; i++) {
        ASSERT_INT_EQ(example_dungeon_npc_describe(running.instance, &description), STASIS_OK);
        ASSERT_SIZE_EQ(description.size, strlen("merchant"));
        ASSERT_MEM_EQ(description.data, "merchant", description.size);
    }
    stop(running);
}

/* A script's constructors run once, when Stasis starts a new instance, and may call host functions.
 * Creating the instance runs none of the script's code, its entry points never run the
 * constructors again, and a restored instance never runs them. */
TEST(constructors_run_once_when_the_instance_starts) {
    const struct running running = create("dungeon_guard");
    ASSERT_STR_EQ(output, "");
    ASSERT_STR_EQ(calls, "");
    ASSERT_INT_EQ(stasis_instance_start(running.instance), STASIS_OK);
    ASSERT_STR_EQ(output, "started as 1\n");
    ASSERT_STR_EQ(calls, "world.find(guard)");
    for (int i = 0; i < 2; i++) {
        forget();
        ASSERT_INT_EQ(example_dungeon_npc_tick(running.instance, frame), STASIS_OK);
        ASSERT_STR_EQ(output, played);
        ASSERT_STR_EQ(calls, played_calls);
    }

    size_t size = snapshot_size(running.instance);
    void *snapshot = malloc(size);
    ASSERT_TRUE(snapshot != NULL);
    ASSERT_INT_EQ(stasis_instance_snapshot(running.instance, snapshot, size, &size), STASIS_OK);
    stasis_handle handles[2];
    size_t handle_count = 0;
    ASSERT_INT_EQ(stasis_instance_handles(running.instance, handles, 2, &handle_count), STASIS_OK);
    ASSERT_SIZE_EQ(handle_count, 2);
    stasis_instance *restored = NULL;
    forget();
    ASSERT_INT_EQ(stasis_instance_restore(running.script, snapshot, size, handles, handle_count,
                                          NULL, NULL, 0, &restored),
                  STASIS_OK);
    free(snapshot);
    ASSERT_INT_EQ(example_dungeon_npc_tick(restored, frame), STASIS_OK);
    ASSERT_STR_EQ(output, played);
    ASSERT_STR_EQ(calls, played_calls);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    stop(running);
}

/* A script provides each optional entry point it defines with the entry point's macro, and no
 * other. */
TEST(a_script_provides_the_optional_entry_points_it_defines_with_their_macros) {
    stasis_library *library = create_library();
    stasis_script *guard_script = load_script(library, &example_dungeon_interface, "dungeon_guard");
    stasis_script *merchant_script =
        load_script(library, &example_dungeon_interface, "dungeon_merchant");
    ASSERT_TRUE(stasis_script_provides(guard_script, EXAMPLE_DUNGEON_NPC_TICK_ENTRY));
    ASSERT_TRUE(stasis_script_provides(guard_script, EXAMPLE_DUNGEON_NPC_ON_HIT_ENTRY));
    ASSERT_TRUE(!stasis_script_provides(guard_script, EXAMPLE_DUNGEON_NPC_DESCRIBE_ENTRY));
    ASSERT_TRUE(stasis_script_provides(merchant_script, EXAMPLE_DUNGEON_NPC_TICK_ENTRY));
    ASSERT_TRUE(!stasis_script_provides(merchant_script, EXAMPLE_DUNGEON_NPC_ON_HIT_ENTRY));
    ASSERT_TRUE(stasis_script_provides(merchant_script, EXAMPLE_DUNGEON_NPC_DESCRIBE_ENTRY));
    ASSERT_INT_EQ(stasis_script_destroy(guard_script), STASIS_OK);
    ASSERT_INT_EQ(stasis_script_destroy(merchant_script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* The strings that host functions return are the script's, which frees them: play() frees the
 * guard's name each tick, which takes many pages, and its memory stays as large as the first tick
 * made it. */
TEST(a_script_frees_the_strings_that_host_functions_return) {
    const struct running running = start("dungeon_guard");
    give_long_name(&guard, 'g');
    ASSERT_INT_EQ(example_dungeon_npc_tick(running.instance, frame), STASIS_OK);
    const size_t size = snapshot_size(running.instance);
    for (int i = 0; i < 8; i++) {
        ASSERT_INT_EQ(example_dungeon_npc_tick(running.instance, frame), STASIS_OK);
    }
    ASSERT_SIZE_EQ(snapshot_size(running.instance), size);
    stop(running);
}

/* An entry point's arguments are borrowed: its export frees the strings in them once the script's
 * function returns, so a weapon of many pages leaves the script's memory as large as the first hit
 * made it. */
TEST(an_entry_point_frees_its_arguments_once_it_returns) {
    const struct running running = start("dungeon_guard");
    example_dungeon_combat_hit blow;
    blow.by = object_of(&rat);
    memset(long_name, 'w', sizeof long_name);
    blow.weapon.data = long_name;
    blow.weapon.size = sizeof long_name;
    blow.damage = 1;
    blow.at.x = 0.0F;
    blow.at.y = 0.0F;
    bool hurts = true;
    ASSERT_INT_EQ(example_dungeon_npc_on_hit(running.instance, blow, &hurts), STASIS_OK);
    const size_t size = snapshot_size(running.instance);
    for (int i = 0; i < 8; i++) {
        ASSERT_INT_EQ(example_dungeon_npc_on_hit(running.instance, blow, &hurts), STASIS_OK);
        ASSERT_TRUE(!hurts);
    }
    ASSERT_SIZE_EQ(snapshot_size(running.instance), size);
    stop(running);
}

/* An entry point's result is copied, and its cabi_post_* frees the copy once Stasis has read it:
 * the merchant describes itself with the name of many pages it keeps, and its memory stays as large
 * as the first description made it. */
TEST(an_entry_point_frees_the_copy_of_its_result) {
    const struct running running = start("dungeon_merchant");
    give_long_name(&merchant, 'm');
    ASSERT_INT_EQ(example_dungeon_npc_tick(running.instance, frame), STASIS_OK);
    stasis_string description = {NULL, 0};
    ASSERT_INT_EQ(example_dungeon_npc_describe(running.instance, &description), STASIS_OK);
    const size_t size = snapshot_size(running.instance);
    for (int i = 0; i < 8; i++) {
        ASSERT_INT_EQ(example_dungeon_npc_describe(running.instance, &description), STASIS_OK);
        ASSERT_SIZE_EQ(description.size, sizeof long_name);
        ASSERT_MEM_EQ(description.data, long_name, sizeof long_name);
    }
    ASSERT_SIZE_EQ(snapshot_size(running.instance), size);
    stop(running);
}

/* The ids of the sections that the tests read. */
enum {
    CUSTOM_SECTION = 0,
    EXPORT_SECTION = 7,
};

/* A module's sections, as a reader of the binary format walks them. */
struct sections {
    const uint8_t *at;
    const uint8_t *end;
};

/* Reads an unsigned LEB128 number of at most 32 bits. */
static uint32_t read_number(const uint8_t **at, const uint8_t *end) {
    uint32_t number = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        ASSERT_TRUE(*at < end);
        const uint8_t byte = *(*at)++;
        number |= (uint32_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            return number;
        }
    }
    ASSERT_TRUE(false);
    return 0;
}

/* Moves to the next section, setting its id and where its contents are, or returns false after the
 * last. */
static bool next_section(struct sections *sections, uint8_t *id, const uint8_t **contents,
                         size_t *size) {
    if (sections->at == sections->end) {
        return false;
    }
    *id = *sections->at++;
    *size = read_number(&sections->at, sections->end);
    ASSERT_TRUE(*size <= (size_t)(sections->end - sections->at));
    *contents = sections->at;
    sections->at += *size;
    return true;
}

/* The sections of a module, after its header. */
static struct sections sections_of(struct module_bytes module) {
    ASSERT_TRUE(module.size >= 8);
    struct sections sections = {module.bytes + 8, module.bytes + module.size};
    return sections;
}

/* Reads a name, setting where its bytes are. */
static size_t read_name(const uint8_t **at, const uint8_t *end, const char **name) {
    const size_t size = read_number(at, end);
    ASSERT_TRUE(size <= (size_t)(end - *at));
    *name = (const char *)*at;
    *at += size;
    return size;
}

/* Joins the names of a module's exports, sorted, with a space between each two. */
static void list_exports(struct module_bytes module, char *list, size_t list_size) {
    const char *names[16];
    size_t sizes[16];
    size_t count = 0;
    struct sections sections = sections_of(module);
    uint8_t id = 0;
    const uint8_t *contents = NULL;
    size_t size = 0;
    while (next_section(&sections, &id, &contents, &size)) {
        if (id != EXPORT_SECTION) {
            continue;
        }
        const uint8_t *end = contents + size;
        count = read_number(&contents, end);
        ASSERT_TRUE(count <= 16);
        for (size_t i = 0; i < count; i++) {
            sizes[i] = read_name(&contents, end, &names[i]);
            contents++;
            (void)read_number(&contents, end);
        }
    }
    /* An insertion sort, by the names' bytes. */
    for (size_t i = 1; i < count; i++) {
        for (size_t j = i; j > 0; j--) {
            const size_t shorter = sizes[j] < sizes[j - 1] ? sizes[j] : sizes[j - 1];
            const int order = memcmp(names[j - 1], names[j], shorter);
            if (order < 0 || (order == 0 && sizes[j - 1] <= sizes[j])) {
                break;
            }
            const char *name = names[j];
            names[j] = names[j - 1];
            names[j - 1] = name;
            const size_t name_size = sizes[j];
            sizes[j] = sizes[j - 1];
            sizes[j - 1] = name_size;
        }
    }
    size_t used = 0;
    list[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        const int length = snprintf(list + used, list_size - used, "%s%.*s", i > 0 ? " " : "",
                                    (int)sizes[i], names[i]);
        ASSERT_TRUE(length >= 0 && (size_t)length < list_size - used);
        used += (size_t)length;
    }
}

/* A C script exports what Stasis calls and nothing else: its memory, its one mutable global, which
 * a snapshot holds, stasis_initialize, cabi_realloc, and its entry points, with the cabi_post_* of
 * each that returns a string. A backend would run `_initialize` or `__wasm_call_ctors` by itself,
 * so neither is among them. */
TEST(a_script_exports_what_stasis_calls) {
    char list[1024];
    struct module_bytes module = read_module("dungeon_guard");
    list_exports(module, list, sizeof list);
    ASSERT_STR_EQ(list,
                  "__stack_pointer cabi_realloc example:dungeon/npc@1#on_hit:1e92fab39e53c5d0 "
                  "example:dungeon/npc@1#tick:37ab64a28242fa4b memory stasis_initialize");
    free_module(module);
    module = read_module("dungeon_merchant");
    list_exports(module, list, sizeof list);
    ASSERT_STR_EQ(list, "__stack_pointer cabi_post_example:dungeon/npc@1#describe:ae6637fc59233091 "
                        "cabi_realloc example:dungeon/npc@1#describe:ae6637fc59233091 "
                        "example:dungeon/npc@1#tick:37ab64a28242fa4b memory stasis_initialize");
    free_module(module);
}

/* The SHA-256 of the notices of wasi-sdk 34's C library, as they were checked against its
 * sources: 13 blocks of 11,711 bytes in all. */
static const uint8_t notices_hash[STASIS_SHA256_SIZE] = {
    0x23, 0xd8, 0x0c, 0x78, 0xd8, 0x23, 0x9f, 0xc2, 0xa4, 0x66, 0xa4, 0x1d, 0x29, 0x14, 0xb6, 0xdf,
    0x0a, 0xa2, 0x54, 0xd5, 0xc2, 0x54, 0x5a, 0xc0, 0xd7, 0x74, 0xa8, 0xc9, 0x88, 0xbd, 0x35, 0x4a,
};

/* Every C script carries the notices of wasi-sdk 34's C library in a custom section of its own, as
 * the generator holds them in wasi_libc_notices.txt. Moving the pinned wasi-sdk means checking them
 * against the new C library's sources, and then this test's version and hash. */
TEST(a_script_carries_the_notices_of_its_c_library) {
    ASSERT_STR_EQ(STASIS_PINNED_WASI_SDK, "34.0");
    FILE *file = fopen(STASIS_NOTICES_FILE, "rb");
    ASSERT_TRUE(file != NULL);
    static char notices[16384];
    const size_t notices_size = fread(notices, 1, sizeof notices, file);
    ASSERT_TRUE(feof(file));
    (void)fclose(file);
    ASSERT_SIZE_EQ(notices_size, 11711);
    stasis_sha256 sha;
    stasis_sha256_init(&sha);
    stasis_sha256_update(&sha, notices, notices_size);
    uint8_t hash[STASIS_SHA256_SIZE];
    stasis_sha256_final(&sha, hash);
    ASSERT_MEM_EQ(hash, notices_hash, sizeof hash);

    struct module_bytes module = read_module("dungeon_guard");
    struct sections sections = sections_of(module);
    uint8_t id = 0;
    const uint8_t *contents = NULL;
    size_t size = 0;
    size_t found = 0;
    while (next_section(&sections, &id, &contents, &size)) {
        const uint8_t *end = contents + size;
        const char *name = NULL;
        if (id != CUSTOM_SECTION || read_name(&contents, end, &name) != 7 ||
            memcmp(name, "notices", 7) != 0) {
            continue;
        }
        found++;
        ASSERT_SIZE_EQ((size_t)(end - contents), notices_size);
        ASSERT_MEM_EQ(contents, notices, notices_size);
    }
    ASSERT_SIZE_EQ(found, 1);
    free_module(module);
}

TEST_LIST(TEST_CASE(every_type_crosses_both_ways),
          TEST_CASE(an_entry_point_takes_a_record_and_returns_a_bool),
          TEST_CASE(an_entry_point_returns_a_string_literal_and_a_string_it_keeps),
          TEST_CASE(constructors_run_once_when_the_instance_starts),
          TEST_CASE(a_script_provides_the_optional_entry_points_it_defines_with_their_macros),
          TEST_CASE(a_script_frees_the_strings_that_host_functions_return),
          TEST_CASE(an_entry_point_frees_its_arguments_once_it_returns),
          TEST_CASE(an_entry_point_frees_the_copy_of_its_result),
          TEST_CASE(a_script_exports_what_stasis_calls),
          TEST_CASE(a_script_carries_the_notices_of_its_c_library));
