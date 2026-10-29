/* Snapshots and restores: a snapshot of a ready instance holds its memory, its mutable globals and
 * its handles, and restores into a new instance that carries on exactly as the original would
 * have. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_c_library.h"
#include "test_calls.h"
#include "test_snapshots.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The kinds of host object, as the host defines them. Only an object's address crosses, so each
 * holds nothing that the tests read. */
struct test_snapshots_world_entity {
    int unused;
};

struct test_snapshots_world_item {
    int unused;
};

/* The size of a page of linear memory. */
enum { PAGE_SIZE = 65536 };

/* The host objects that `find` and `pick_up` hand out, by index. */
enum { HOST_OBJECT_COUNT = 16 };

static test_snapshots_world_entity entities[HOST_OBJECT_COUNT];
static test_snapshots_world_item items[HOST_OBJECT_COUNT];

/* The host object `touch` was last given, how many times it has been called, and what it does
 * besides, if the case has set it, with the instance that called it. */
static const void *touched;
static unsigned touches;
static void (*while_touching)(stasis_instance *instance);

test_snapshots_world_entity *test_snapshots_world_find(stasis_instance *instance, uint32_t index) {
    (void)instance;
    ASSERT_TRUE(index < HOST_OBJECT_COUNT);
    return &entities[index];
}

test_snapshots_world_item *test_snapshots_world_pick_up(stasis_instance *instance, uint32_t index) {
    (void)instance;
    ASSERT_TRUE(index < HOST_OBJECT_COUNT);
    return &items[index];
}

void test_snapshots_world_touch(stasis_instance *instance, test_snapshots_world_entity *target) {
    touched = target;
    touches++;
    if (while_touching != NULL) {
        while_touching(instance);
    }
}

/* A snapshot, in memory of the test's own. */
struct snapshot {
    unsigned char *bytes;
    size_t size;
};

/* Takes a snapshot of `instance`, which must succeed. */
static struct snapshot take_snapshot(const stasis_instance *instance) {
    struct snapshot snapshot = {NULL, 0};
    ASSERT_INT_EQ(stasis_instance_snapshot_size(instance, &snapshot.size), STASIS_OK);
    ASSERT_TRUE(snapshot.size > 0);
    snapshot.bytes = malloc(snapshot.size);
    ASSERT_TRUE(snapshot.bytes != NULL);
    size_t written = 0;
    ASSERT_INT_EQ(stasis_instance_snapshot(instance, snapshot.bytes, snapshot.size, &written),
                  STASIS_OK);
    ASSERT_SIZE_EQ(written, snapshot.size);
    return snapshot;
}

/* Frees what take_snapshot or a copy took. */
static void free_snapshot(struct snapshot snapshot) { free(snapshot.bytes); }

/* Restores `snapshot` into a new instance of `script`, binding the `count` handles at `handles`,
 * which must succeed with an empty message. */
static stasis_instance *restore(stasis_script *script, struct snapshot snapshot,
                                const stasis_handle *handles, size_t count) {
    char message[256] = "not written";
    stasis_instance *restored = NULL;
    ASSERT_INT_EQ(stasis_instance_restore(script, snapshot.bytes, snapshot.size, handles, count,
                                          NULL, message, sizeof message, &restored),
                  STASIS_OK);
    ASSERT_STR_EQ(message, "");
    ASSERT_TRUE(restored != NULL);
    return restored;
}

/* An instance of the script snapshots.wat, started, with its script and library. */
struct running {
    stasis_library *library;
    stasis_script *script;
    stasis_instance *instance;
};

/* Starts an instance of snapshots.wat in a library of its own, having forgotten every touch. */
static struct running start_snapshots(void) {
    touched = NULL;
    touches = 0;
    while_touching = NULL;
    struct running running;
    running.library = create_library();
    running.script = load_script(running.library, &test_snapshots_interface, "snapshots");
    running.instance = start_instance(running.script);
    return running;
}

/* Destroys the instance, its script and its library, each of which must succeed. */
static void stop(struct running running) {
    destroy_all(running.instance, running.script, running.library);
}

/* The 4 bytes at `address` in the memory of `instance`. */
static uint32_t load(stasis_instance *instance, uint32_t address) {
    uint32_t value = 0;
    ASSERT_INT_EQ(test_snapshots_keeper_load(instance, address, &value), STASIS_OK);
    return value;
}

/* A restored instance's memory holds what the original's did when the snapshot was taken, whatever
 * either does after, and the restore keeps none of the snapshot's bytes, which the host may reuse
 * at once. */
TEST(a_restored_instance_carries_on_with_the_memory_it_had) {
    const struct running running = start_snapshots();
    ASSERT_INT_EQ(test_snapshots_keeper_store(running.instance, 100, 0xDEADBEEFU), STASIS_OK);
    const struct snapshot snapshot = take_snapshot(running.instance);
    ASSERT_INT_EQ(test_snapshots_keeper_store(running.instance, 100, 1), STASIS_OK);

    stasis_instance *restored = restore(running.script, snapshot, NULL, 0);
    memset(snapshot.bytes, 0xFF, snapshot.size);
    ASSERT_UINT_EQ(load(restored, 100), 0xDEADBEEFU);
    ASSERT_UINT_EQ(load(running.instance, 100), 1);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    stop(running);
}

/* Checks that two snapshots hold the same bytes. */
static void expect_same_snapshot(struct snapshot actual, struct snapshot expected) {
    ASSERT_SIZE_EQ(actual.size, expected.size);
    ASSERT_MEM_EQ(actual.bytes, expected.bytes, expected.size);
}

/* A restored instance is never started, so its script's initialisation, which ran once in the
 * instance the snapshot was taken of, never runs again, and starting it is refused. */
TEST(a_restored_instance_is_ready_without_starting) {
    const struct running running = start_snapshots();
    const struct snapshot snapshot = take_snapshot(running.instance);
    stasis_instance *restored = restore(running.script, snapshot, NULL, 0);
    uint32_t starts = 0;
    ASSERT_INT_EQ(test_snapshots_keeper_starts(restored, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 1);
    ASSERT_INT_EQ(stasis_instance_start(restored), STASIS_REFUSED_STARTED);
    ASSERT_INT_EQ(test_snapshots_keeper_starts(restored, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 1);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    stop(running);
}

/* Checks the mutable globals of `instance`, beside the count of starts. */
static void expect_globals(stasis_instance *instance, uint32_t a, uint64_t b, float c, double d) {
    uint32_t narrow = 0;
    uint64_t wide = 0;
    float single = 0;
    double twice = 0;
    ASSERT_INT_EQ(test_snapshots_keeper_global_i32(instance, &narrow), STASIS_OK);
    ASSERT_INT_EQ(test_snapshots_keeper_global_i64(instance, &wide), STASIS_OK);
    ASSERT_INT_EQ(test_snapshots_keeper_global_f32(instance, &single), STASIS_OK);
    ASSERT_INT_EQ(test_snapshots_keeper_global_f64(instance, &twice), STASIS_OK);
    ASSERT_UINT_EQ(narrow, a);
    ASSERT_UINT_EQ(wide, b);
    ASSERT_FLOAT_EQ(single, c);
    ASSERT_DOUBLE_EQ(twice, d);
}

/* Every mutable global keeps its value, whatever its type, and each instance keeps its own after
 * the restore. */
TEST(a_restored_instance_carries_on_with_the_globals_it_had) {
    const struct running running = start_snapshots();
    ASSERT_INT_EQ(test_snapshots_keeper_set_globals(running.instance, 0x89ABCDEFU,
                                                    0x0123456789ABCDEFU, -1.5F, 6.02214076e23),
                  STASIS_OK);
    const struct snapshot snapshot = take_snapshot(running.instance);
    ASSERT_INT_EQ(test_snapshots_keeper_set_globals(running.instance, 1, 2, 3, 4), STASIS_OK);

    stasis_instance *restored = restore(running.script, snapshot, NULL, 0);
    expect_globals(restored, 0x89ABCDEFU, 0x0123456789ABCDEFU, -1.5F, 6.02214076e23);
    expect_globals(running.instance, 1, 2, 3, 4);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    stop(running);
}

/* The number of pages of the memory of `instance`. */
static uint32_t pages(stasis_instance *instance) {
    uint32_t count = 0;
    ASSERT_INT_EQ(test_snapshots_keeper_pages(instance, &count), STASIS_OK);
    return count;
}

/* Grows the memory of `instance` by `by` pages, and returns what the script's memory.grow did. */
static uint32_t grow(stasis_instance *instance, uint32_t by) {
    uint32_t before = 0;
    ASSERT_INT_EQ(test_snapshots_keeper_grow(instance, by, &before), STASIS_OK);
    return before;
}

/* Memory that grew between two snapshots restores at the size it had grown to, with what the new
 * pages hold, and grows on from there as the original does, up to the most the script allows. */
TEST(memory_that_grew_between_snapshots_restores_at_its_size) {
    const struct running running = start_snapshots();
    const struct snapshot first = take_snapshot(running.instance);
    ASSERT_UINT_EQ(grow(running.instance, 2), 1);
    ASSERT_INT_EQ(test_snapshots_keeper_store(running.instance, (3 * PAGE_SIZE) - 4, 0x5A5A5A5AU),
                  STASIS_OK);
    const struct snapshot second = take_snapshot(running.instance);
    ASSERT_SIZE_EQ(second.size, first.size + ((size_t)2 * PAGE_SIZE));

    stasis_instance *early = restore(running.script, first, NULL, 0);
    stasis_instance *late = restore(running.script, second, NULL, 0);
    ASSERT_UINT_EQ(pages(early), 1);
    ASSERT_UINT_EQ(pages(late), 3);
    ASSERT_UINT_EQ(load(late, (3 * PAGE_SIZE) - 4), 0x5A5A5A5AU);
    ASSERT_UINT_EQ(grow(late, 2), 0xFFFFFFFFU);
    ASSERT_UINT_EQ(grow(running.instance, 2), 0xFFFFFFFFU);
    ASSERT_UINT_EQ(grow(late, 1), 3);
    ASSERT_UINT_EQ(grow(running.instance, 1), 3);
    ASSERT_UINT_EQ(load(late, (4 * PAGE_SIZE) - 4), 0);
    ASSERT_UINT_EQ(grow(early, 3), 1);
    ASSERT_UINT_EQ(load(early, (3 * PAGE_SIZE) - 4), 0);
    ASSERT_INT_EQ(stasis_instance_destroy(early), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_destroy(late), STASIS_OK);
    free_snapshot(first);
    free_snapshot(second);
    stop(running);
}

/* Makes a string of a C string. */
static stasis_string text(const char *string) {
    const stasis_string made = {string, strlen(string)};
    return made;
}

/* Checks that `recall` returns `expected` from `instance`. */
static void expect_recall(stasis_instance *instance, const char *expected) {
    stasis_string recalled = {NULL, 0};
    ASSERT_INT_EQ(test_snapshots_keeper_recall(instance, &recalled), STASIS_OK);
    ASSERT_SIZE_EQ(recalled.size, strlen(expected));
    ASSERT_MEM_EQ(recalled.data, expected, recalled.size);
}

/* A string that the script keeps in its memory comes back from a restored instance as a result,
 * as it does from the original. */
TEST(a_restored_instance_returns_the_strings_it_kept) {
    const struct running running = start_snapshots();
    ASSERT_INT_EQ(test_snapshots_keeper_remember(running.instance, text("a brass lantern")),
                  STASIS_OK);
    const struct snapshot snapshot = take_snapshot(running.instance);
    ASSERT_INT_EQ(test_snapshots_keeper_remember(running.instance, text("a rope")), STASIS_OK);

    stasis_instance *restored = restore(running.script, snapshot, NULL, 0);
    expect_recall(restored, "a brass lantern");
    expect_recall(running.instance, "a rope");
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    stop(running);
}

/* A restored instance's own snapshot is the snapshot it was restored from, and after the same
 * calls, the original and the restored instance give the same snapshots again. */
TEST(the_same_state_gives_the_same_snapshot) {
    const struct running running = start_snapshots();
    ASSERT_INT_EQ(test_snapshots_keeper_set_globals(running.instance, 7, 8, 9, 10), STASIS_OK);
    ASSERT_INT_EQ(test_snapshots_keeper_remember(running.instance, text("ash")), STASIS_OK);
    const struct snapshot snapshot = take_snapshot(running.instance);
    stasis_instance *restored = restore(running.script, snapshot, NULL, 0);
    const struct snapshot again = take_snapshot(restored);
    expect_same_snapshot(again, snapshot);

    stasis_instance *both[2] = {running.instance, restored};
    struct snapshot later[2];
    for (size_t i = 0; i < 2; i++) {
        ASSERT_UINT_EQ(grow(both[i], 1), 1);
        ASSERT_INT_EQ(test_snapshots_keeper_remember(both[i], text("embers")), STASIS_OK);
        later[i] = take_snapshot(both[i]);
    }
    expect_same_snapshot(later[1], later[0]);
    ASSERT_TRUE(later[0].size != snapshot.size ||
                memcmp(later[0].bytes, snapshot.bytes, snapshot.size) != 0);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    for (size_t i = 0; i < 2; i++) {
        free_snapshot(later[i]);
    }
    free_snapshot(again);
    free_snapshot(snapshot);
    stop(running);
}

/* The handle that `find` gives `instance` for the entity at `index`, and that `pick_up` gives it
 * for the item. */
static uint32_t find(stasis_instance *instance, uint32_t index) {
    uint32_t handle = 0;
    ASSERT_INT_EQ(test_snapshots_keeper_find(instance, index, &handle), STASIS_OK);
    return handle;
}

static uint32_t pick_up(stasis_instance *instance, uint32_t index) {
    uint32_t handle = 0;
    ASSERT_INT_EQ(test_snapshots_keeper_pick_up(instance, index, &handle), STASIS_OK);
    return handle;
}

/* The host object that `touch` gives the host for `handle` in `instance`. */
static const void *touch(stasis_instance *instance, uint32_t handle) {
    static const int untouched = 0;
    touched = &untouched;
    ASSERT_INT_EQ(test_snapshots_keeper_touch(instance, handle), STASIS_OK);
    return touched;
}

/* The most handles a case lists. */
enum { MOST_HANDLES = 8 };

/* Lists the live handles of `instance` into `handles`, which hold MOST_HANDLES, and returns how
 * many there are. */
static size_t list_handles(const stasis_instance *instance, stasis_handle *handles) {
    size_t count = 0;
    ASSERT_INT_EQ(stasis_instance_handle_count(instance, &count), STASIS_OK);
    ASSERT_TRUE(count <= MOST_HANDLES);
    size_t listed = 99;
    ASSERT_INT_EQ(stasis_instance_handles(instance, handles, MOST_HANDLES, &listed), STASIS_OK);
    ASSERT_SIZE_EQ(listed, count);
    return count;
}

/* Checks that `handle` is the handle `value`, for `host_object` of the kind `kind`. */
static void expect_handle(stasis_handle handle, uint32_t value, uint32_t kind,
                          const void *host_object) {
    ASSERT_UINT_EQ(handle.value, value);
    ASSERT_UINT_EQ(handle.kind, kind);
    ASSERT_TRUE(handle.host_object == host_object);
}

/* An instance lists its live handles in increasing order of value, with their kinds and host
 * objects, and leaves out those it has had revoked. */
TEST(an_instance_lists_its_live_handles_in_order) {
    const struct running running = start_snapshots();
    stasis_handle handles[MOST_HANDLES];
    ASSERT_SIZE_EQ(list_handles(running.instance, handles), 0);
    ASSERT_UINT_EQ(find(running.instance, 3), 1);
    ASSERT_UINT_EQ(pick_up(running.instance, 3), 2);
    ASSERT_UINT_EQ(find(running.instance, 5), 3);
    ASSERT_UINT_EQ(find(running.instance, 4), 4);
    ASSERT_INT_EQ(
        stasis_instance_revoke(running.instance, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, &entities[5]),
        STASIS_OK);
    ASSERT_UINT_EQ(find(running.instance, 6), 5);

    ASSERT_SIZE_EQ(list_handles(running.instance, handles), 4);
    expect_handle(handles[0], 1, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, &entities[3]);
    expect_handle(handles[1], 2, TEST_SNAPSHOTS_WORLD_ITEM_KIND, &items[3]);
    expect_handle(handles[2], 4, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, &entities[4]);
    expect_handle(handles[3], 5, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, &entities[6]);
    stop(running);
}

/* A restore binds each live handle to the host object the host gives it, which may be a new one,
 * or revokes it. A handle that wasn't live in the snapshot stays revoked, and a host object new to
 * the restored instance gets a value it has never had. */
TEST(a_restore_binds_handles_to_new_host_objects) {
    const struct running running = start_snapshots();
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    ASSERT_UINT_EQ(find(running.instance, 1), 2);
    ASSERT_UINT_EQ(pick_up(running.instance, 0), 3);
    ASSERT_UINT_EQ(find(running.instance, 2), 4);
    ASSERT_INT_EQ(
        stasis_instance_revoke(running.instance, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, &entities[1]),
        STASIS_OK);
    const struct snapshot snapshot = take_snapshot(running.instance);
    stasis_handle handles[MOST_HANDLES];
    ASSERT_SIZE_EQ(list_handles(running.instance, handles), 3);

    /* The host's objects have moved, as they do when it loads a save, and the entity behind handle
     * 4 has gone. The entries go in any order. */
    const stasis_handle bound[3] = {
        {4, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, NULL},
        {1, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, &entities[10]},
        {3, TEST_SNAPSHOTS_WORLD_ITEM_KIND, &items[11]},
    };
    stasis_instance *restored = restore(running.script, snapshot, bound, 3);
    stasis_handle listed[MOST_HANDLES];
    ASSERT_SIZE_EQ(list_handles(restored, listed), 2);
    expect_handle(listed[0], 1, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, &entities[10]);
    expect_handle(listed[1], 3, TEST_SNAPSHOTS_WORLD_ITEM_KIND, &items[11]);

    ASSERT_TRUE(touch(restored, 1) == &entities[10]);
    ASSERT_TRUE(touch(restored, 2) == NULL);
    ASSERT_TRUE(touch(restored, 4) == NULL);
    ASSERT_UINT_EQ(find(restored, 10), 1);
    ASSERT_UINT_EQ(pick_up(restored, 11), 3);
    ASSERT_UINT_EQ(find(restored, 0), 5);
    ASSERT_UINT_EQ(find(restored, 2), 6);
    ASSERT_UINT_EQ(find(running.instance, 3), 5);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    stop(running);
}

/* A handle at or past the next value the snapshot records was never issued, so it's forged in the
 * restored instance, as in the original, and a script that passes it to a host function traps. */
TEST(a_handle_never_issued_is_forged_after_a_restore) {
    const struct running running = start_snapshots();
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    const struct snapshot snapshot = take_snapshot(running.instance);
    const stasis_handle bound = {1, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, &entities[0]};
    stasis_instance *restored = restore(running.script, snapshot, &bound, 1);
    ASSERT_INT_EQ(test_snapshots_keeper_touch(restored, 2), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(restored);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_BOUNDARY_VIOLATION);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    stop(running);
}

/* What the output hook has been given, by instance, for the two that a case compares. */
struct output {
    const stasis_instance *instance;
    char text[64];
    size_t size;
};

static struct output outputs[2];

static void record_output(stasis_instance *instance, void *user_data, stasis_stream stream,
                          const char *bytes, size_t size) {
    (void)user_data;
    ASSERT_INT_EQ(stream, STASIS_STREAM_STDOUT);
    for (size_t i = 0; i < 2; i++) {
        struct output *output = &outputs[i];
        if (output->instance == instance) {
            ASSERT_TRUE(size < sizeof output->text - output->size);
            memcpy(output->text + output->size, bytes, size);
            output->size += size;
            output->text[output->size] = '\0';
            return;
        }
    }
    ASSERT_TRUE(false);
}

/* A C script's stdout holds a line without its newline in its memory, so the line travels in the
 * snapshot, and the restored instance writes it out with the rest of the line, as the original
 * does. */
TEST(an_unfinished_line_of_output_travels_in_a_snapshot) {
    memset(outputs, 0, sizeof outputs);
    stasis_library_settings settings;
    memset(&settings, 0, sizeof settings);
    settings.output = record_output;
    stasis_library *library = NULL;
    ASSERT_INT_EQ(stasis_library_create(&settings, &library), STASIS_OK);
    stasis_script *script = load_script(library, &test_c_library_interface, "c_library");
    stasis_instance *instance = start_instance(script);
    outputs[0].instance = instance;
    ASSERT_INT_EQ(test_c_library_user_print_partial(instance), STASIS_OK);
    ASSERT_STR_EQ(outputs[0].text, "");
    const struct snapshot snapshot = take_snapshot(instance);

    stasis_instance *restored = restore(script, snapshot, NULL, 0);
    outputs[1].instance = restored;
    ASSERT_INT_EQ(test_c_library_user_print_line(restored), STASIS_OK);
    ASSERT_INT_EQ(test_c_library_user_print_line(instance), STASIS_OK);
    ASSERT_STR_EQ(outputs[1].text, "partialhello, world\n");
    ASSERT_STR_EQ(outputs[0].text, "partialhello, world\n");
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    destroy_all(instance, script, library);
}

/* A C script built with the C language pack's flags keeps its stack pointer in a mutable global,
 * which travels in the snapshot, beside its memory, so a restored instance carries on, and isn't
 * initialised again. */
TEST(a_c_script_restores) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "c_script");
    stasis_instance *instance = start_instance(script);
    const struct snapshot snapshot = take_snapshot(instance);
    stasis_instance *restored = restore(script, snapshot, NULL, 0);
    uint32_t starts = 0;
    ASSERT_INT_EQ(test_calls_caller_starts(restored, &starts), STASIS_OK);
    ASSERT_UINT_EQ(starts, 1);
    const struct snapshot again = take_snapshot(restored);
    expect_same_snapshot(again, snapshot);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(again);
    free_snapshot(snapshot);
    destroy_all(instance, script, library);
}

/* Checks that `instance` can't be snapshotted, with the refusal `refused`, whether the host asks
 * for the size or for the snapshot, and that its handles can still be listed. */
static void expect_refused(const stasis_instance *instance, stasis_status refused) {
    size_t size = 99;
    ASSERT_INT_EQ(stasis_instance_snapshot_size(instance, &size), refused);
    ASSERT_SIZE_EQ(size, 0);
    unsigned char buffer[1024];
    size = 99;
    ASSERT_INT_EQ(stasis_instance_snapshot(instance, buffer, sizeof buffer, &size), refused);
    ASSERT_SIZE_EQ(size, 0);
    size_t count = 99;
    ASSERT_INT_EQ(stasis_instance_handle_count(instance, &count), STASIS_OK);
    stasis_handle handles[MOST_HANDLES];
    ASSERT_INT_EQ(stasis_instance_handles(instance, handles, MOST_HANDLES, &count), STASIS_OK);
}

/* An instance of a script that isn't snapshottable can't be snapshotted, and a restore with such a
 * script is refused, whatever the snapshot, with an empty message. */
TEST(a_script_that_isnt_snapshottable_is_refused) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "unsnapshottable");
    stasis_instance *instance = start_instance(script);
    expect_refused(instance, STASIS_REFUSED_NOT_SNAPSHOTTABLE);

    stasis_script *other = load_script(library, &test_snapshots_interface, "snapshots");
    stasis_instance *snapshotted = start_instance(other);
    const struct snapshot snapshot = take_snapshot(snapshotted);
    char message[64] = "not written";
    stasis_instance *restored = instance;
    ASSERT_INT_EQ(stasis_instance_restore(script, snapshot.bytes, snapshot.size, NULL, 0, NULL,
                                          message, sizeof message, &restored),
                  STASIS_REFUSED_NOT_SNAPSHOTTABLE);
    ASSERT_TRUE(restored == NULL);
    ASSERT_STR_EQ(message, "");
    free_snapshot(snapshot);
    ASSERT_INT_EQ(stasis_instance_destroy(snapshotted), STASIS_OK);
    ASSERT_INT_EQ(stasis_script_destroy(other), STASIS_OK);
    destroy_all(instance, script, library);
}

/* Only a ready instance can be snapshotted: not one that was created and hasn't started, nor one
 * that has trapped. */
TEST(only_a_ready_instance_is_snapshotted) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = NULL;
    ASSERT_INT_EQ(stasis_instance_create(script, NULL, &instance), STASIS_OK);
    expect_refused(instance, STASIS_REFUSED_NOT_STARTED);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_OK);
    ASSERT_INT_EQ(test_calls_caller_fail(instance), STASIS_TRAP);
    expect_refused(instance, STASIS_REFUSED_TRAPPED);
    destroy_all(instance, script, library);
}

/* What the host function saw of the instance that called it. */
static stasis_status snapshot_while_running;
static size_t handles_while_running;

static void snapshot_caller(stasis_instance *instance) {
    size_t size = 99;
    snapshot_while_running = stasis_instance_snapshot_size(instance, &size);
    ASSERT_SIZE_EQ(size, 0);
    ASSERT_INT_EQ(stasis_instance_handle_count(instance, &handles_while_running), STASIS_OK);
}

/* An instance that's running, as it is while a host function runs, can't be snapshotted, but its
 * handles can be listed. */
TEST(a_running_instance_isnt_snapshotted) {
    const struct running running = start_snapshots();
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    while_touching = snapshot_caller;
    snapshot_while_running = STASIS_OK;
    ASSERT_INT_EQ(test_snapshots_keeper_touch(running.instance, 1), STASIS_OK);
    ASSERT_UINT_EQ(touches, 1);
    ASSERT_INT_EQ(snapshot_while_running, STASIS_REFUSED_RUNNING);
    ASSERT_SIZE_EQ(handles_while_running, 1);
    stop(running);
}

/* A buffer smaller than the snapshot, or an array smaller than the number of live handles, gets
 * nothing written into it. */
TEST(a_buffer_too_small_is_refused) {
    const struct running running = start_snapshots();
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    ASSERT_UINT_EQ(find(running.instance, 1), 2);
    size_t size = 0;
    ASSERT_INT_EQ(stasis_instance_snapshot_size(running.instance, &size), STASIS_OK);
    unsigned char *buffer = malloc(size);
    ASSERT_TRUE(buffer != NULL);
    memset(buffer, 0xAA, size);
    size_t written = 99;
    ASSERT_INT_EQ(stasis_instance_snapshot(running.instance, buffer, size - 1, &written),
                  STASIS_BUFFER_TOO_SMALL);
    ASSERT_SIZE_EQ(written, 0);
    for (size_t i = 0; i < size; i++) {
        ASSERT_UINT_EQ(buffer[i], 0xAA);
    }
    written = 99;
    ASSERT_INT_EQ(stasis_instance_snapshot(running.instance, NULL, 0, &written),
                  STASIS_BUFFER_TOO_SMALL);
    ASSERT_SIZE_EQ(written, 0);
    free(buffer);

    stasis_handle handles[2];
    memset(handles, 0xAA, sizeof handles);
    size_t count = 99;
    ASSERT_INT_EQ(stasis_instance_handles(running.instance, handles, 1, &count),
                  STASIS_BUFFER_TOO_SMALL);
    ASSERT_SIZE_EQ(count, 0);
    ASSERT_UINT_EQ(handles[0].value, 0xAAAAAAAAU);
    stop(running);
}

/* A snapshot fits a buffer larger than it, at its start, and an instance with no handles lists
 * none into no array at all. */
TEST(a_buffer_larger_than_the_snapshot_holds_it) {
    const struct running running = start_snapshots();
    const struct snapshot snapshot = take_snapshot(running.instance);
    unsigned char *buffer = malloc(snapshot.size + 16);
    ASSERT_TRUE(buffer != NULL);
    size_t written = 0;
    ASSERT_INT_EQ(stasis_instance_snapshot(running.instance, buffer, snapshot.size + 16, &written),
                  STASIS_OK);
    ASSERT_SIZE_EQ(written, snapshot.size);
    ASSERT_MEM_EQ(buffer, snapshot.bytes, snapshot.size);
    free(buffer);
    size_t count = 99;
    ASSERT_INT_EQ(stasis_instance_handles(running.instance, NULL, 0, &count), STASIS_OK);
    ASSERT_SIZE_EQ(count, 0);
    free_snapshot(snapshot);
    stop(running);
}

/* Each argument that's needed is checked, and a call with one that fails its check does nothing
 * else, and leaves its out-parameters empty. */
TEST(arguments_are_checked) {
    const struct running running = start_snapshots();
    stasis_instance *instance = running.instance;
    size_t size = 99;
    ASSERT_INT_EQ(stasis_instance_snapshot_size(NULL, &size), STASIS_INVALID_ARGUMENT);
    ASSERT_SIZE_EQ(size, 0);
    ASSERT_INT_EQ(stasis_instance_snapshot_size(instance, NULL), STASIS_INVALID_ARGUMENT);
    unsigned char buffer[16];
    size = 99;
    ASSERT_INT_EQ(stasis_instance_snapshot(NULL, buffer, sizeof buffer, &size),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_SIZE_EQ(size, 0);
    ASSERT_INT_EQ(stasis_instance_snapshot(instance, buffer, sizeof buffer, NULL),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_instance_snapshot(instance, NULL, 1, &size), STASIS_INVALID_ARGUMENT);
    size_t count = 99;
    ASSERT_INT_EQ(stasis_instance_handle_count(NULL, &count), STASIS_INVALID_ARGUMENT);
    ASSERT_SIZE_EQ(count, 0);
    ASSERT_INT_EQ(stasis_instance_handle_count(instance, NULL), STASIS_INVALID_ARGUMENT);
    stasis_handle handles[1];
    count = 99;
    ASSERT_INT_EQ(stasis_instance_handles(NULL, handles, 1, &count), STASIS_INVALID_ARGUMENT);
    ASSERT_SIZE_EQ(count, 0);
    ASSERT_INT_EQ(stasis_instance_handles(instance, handles, 1, NULL), STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_instance_handles(instance, NULL, 1, &count), STASIS_INVALID_ARGUMENT);

    const struct snapshot snapshot = take_snapshot(instance);
    stasis_script *script = running.script;
    char message[64] = "not written";
    stasis_instance *restored = instance;
    ASSERT_INT_EQ(stasis_instance_restore(NULL, snapshot.bytes, snapshot.size, NULL, 0, NULL,
                                          message, sizeof message, &restored),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_TRUE(restored == NULL);
    ASSERT_STR_EQ(message, "");
    ASSERT_INT_EQ(stasis_instance_restore(script, snapshot.bytes, snapshot.size, NULL, 0, NULL,
                                          message, sizeof message, NULL),
                  STASIS_INVALID_ARGUMENT);
    restored = instance;
    ASSERT_INT_EQ(stasis_instance_restore(script, NULL, snapshot.size, NULL, 0, NULL, message,
                                          sizeof message, &restored),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_TRUE(restored == NULL);
    ASSERT_INT_EQ(stasis_instance_restore(script, snapshot.bytes, snapshot.size, NULL, 1, NULL,
                                          message, sizeof message, &restored),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_INT_EQ(stasis_instance_restore(script, snapshot.bytes, snapshot.size, NULL, 0, NULL,
                                          NULL, 1, &restored),
                  STASIS_INVALID_ARGUMENT);
    ASSERT_TRUE(restored == NULL);

    /* Without a message buffer, a restore still works. */
    ASSERT_INT_EQ(stasis_instance_restore(script, snapshot.bytes, snapshot.size, NULL, 0, NULL,
                                          NULL, 0, &restored),
                  STASIS_OK);
    ASSERT_TRUE(restored != NULL);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    stop(running);
}

/* A restored instance has the user data the restore gave it, not the original's. */
TEST(a_restored_instance_has_its_own_user_data) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_snapshots_interface, "snapshots");
    int original_data = 0;
    int restored_data = 0;
    stasis_instance *instance = NULL;
    ASSERT_INT_EQ(stasis_instance_create(script, &original_data, &instance), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_OK);
    const struct snapshot snapshot = take_snapshot(instance);
    stasis_instance *restored = NULL;
    ASSERT_INT_EQ(stasis_instance_restore(script, snapshot.bytes, snapshot.size, NULL, 0,
                                          &restored_data, NULL, 0, &restored),
                  STASIS_OK);
    ASSERT_TRUE(stasis_instance_user_data(restored) == &restored_data);
    ASSERT_TRUE(stasis_instance_user_data(instance) == &original_data);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    destroy_all(instance, script, library);
}

/* A restored instance belongs to its script as a created one does, so the script can't be
 * destroyed while it exists. */
TEST(a_restored_instance_belongs_to_its_script) {
    const struct running running = start_snapshots();
    const struct snapshot snapshot = take_snapshot(running.instance);
    ASSERT_INT_EQ(stasis_instance_destroy(running.instance), STASIS_OK);
    stasis_instance *restored = restore(running.script, snapshot, NULL, 0);
    ASSERT_INT_EQ(stasis_script_destroy(running.script), STASIS_REFUSED_IN_USE);
    free_snapshot(snapshot);
    destroy_all(restored, running.script, running.library);
}

/* Where the fields of a snapshot are, as its format lays them out, every field little-endian: an
 * 8-byte magic number, the format's version as a u32, the 32 bytes of the script's identity, the
 * memory's length as a u64, the mutable globals, the handle table, and the memory's bytes. */
enum {
    VERSION_AT = 8,
    IDENTITY_AT = 12,
    MEMORY_SIZE_AT = 44,
    GLOBALS_AT = 52,
};

static uint32_t get_u32(const unsigned char *at) {
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static void put_u32(unsigned char *at, uint32_t value) {
    for (size_t i = 0; i < 4; i++) {
        at[i] = (unsigned char)(value >> (8 * i));
    }
}

static void put_u64(unsigned char *at, uint64_t value) {
    for (size_t i = 0; i < 8; i++) {
        at[i] = (unsigned char)(value >> (8 * i));
    }
}

/* The offset of the `index`th of a snapshot's mutable globals, each its index as a u32, its type as
 * a byte, and its value, of 8 bytes for an i64 (0x7e) or an f64 (0x7c), and 4 otherwise. With
 * `index` the number of globals, it's the offset of the handle table. */
static size_t global_at(struct snapshot snapshot, uint32_t index) {
    size_t at = GLOBALS_AT + 4;
    for (uint32_t i = 0; i < index; i++) {
        ASSERT_TRUE(at + 4 < snapshot.size);
        const unsigned char type = snapshot.bytes[at + 4];
        at += 4 + 1 + (type == 0x7e || type == 0x7c ? 8U : 4U);
    }
    return at;
}

/* The offset of a snapshot's handle table: the next handle value as a u64, the number of live
 * handles as a u32, and then each live handle, as its value as a u32, and its kind's qualified
 * name, as a u32 that counts its bytes and the bytes. */
static size_t handles_at(struct snapshot snapshot) {
    return global_at(snapshot, get_u32(snapshot.bytes + GLOBALS_AT));
}

/* The offset of the `index`th of a snapshot's live handles. */
static size_t handle_at(struct snapshot snapshot, uint32_t index) {
    size_t at = handles_at(snapshot) + 8 + 4;
    for (uint32_t i = 0; i < index; i++) {
        at += 4 + 4 + get_u32(snapshot.bytes + at + 4);
    }
    return at;
}

/* A snapshot whose bytes are those of `snapshot`, from `from` up to `to`, then `inserted` bytes of
 * zero, then the rest from `to`, in memory of its own. */
static struct snapshot splice(struct snapshot snapshot, size_t from, size_t to, size_t inserted) {
    struct snapshot spliced = {NULL, snapshot.size - (to - from) + inserted};
    spliced.bytes = calloc(spliced.size == 0 ? 1 : spliced.size, 1);
    ASSERT_TRUE(spliced.bytes != NULL);
    memcpy(spliced.bytes, snapshot.bytes, from);
    memcpy(spliced.bytes + from + inserted, snapshot.bytes + to, snapshot.size - to);
    return spliced;
}

/* A copy of `snapshot`, in memory of its own. */
static struct snapshot copy(struct snapshot snapshot) { return splice(snapshot, 0, 0, 0); }

/* Checks that restoring `snapshot` into `script`, binding the `count` handles at `handles`, fails
 * with `status` and `message`, and creates nothing. */
static void expect_refused_restore(stasis_script *script, struct snapshot snapshot,
                                   const stasis_handle *handles, size_t count, stasis_status status,
                                   const char *message) {
    char written[512] = "not written";
    static int not_restored;
    stasis_instance *restored = (stasis_instance *)(void *)&not_restored;
    ASSERT_INT_EQ(stasis_instance_restore(script, snapshot.bytes, snapshot.size, handles, count,
                                          NULL, written, sizeof written, &restored),
                  status);
    ASSERT_TRUE(restored == NULL);
    ASSERT_STR_EQ(written, message);
}

/* Checks that restoring `snapshot` into `script` fails as a malformed snapshot with `message`, and
 * frees the snapshot. */
static void expect_malformed(stasis_script *script, struct snapshot snapshot, const char *message) {
    expect_refused_restore(script, snapshot, NULL, 0, STASIS_INVALID_SNAPSHOT, message);
    free_snapshot(snapshot);
}

/* A snapshot of another script is refused, and the message names both scripts by the start of
 * their identities, whose first 8 bytes it gives as hex. */
TEST(a_snapshot_of_another_script_is_refused) {
    const struct running running = start_snapshots();
    stasis_script *other = load_script(running.library, &test_calls_interface, "calls");
    stasis_instance *instance = start_instance(other);
    const struct snapshot snapshot = take_snapshot(instance);
    char message[256];
    char theirs[17];
    char ours[17];
    for (size_t i = 0; i < 8; i++) {
        (void)snprintf(theirs + (2 * i), 3, "%02x", stasis_script_identity(other)[i]);
        (void)snprintf(ours + (2 * i), 3, "%02x", stasis_script_identity(running.script)[i]);
    }
    (void)snprintf(message, sizeof message,
                   "the snapshot is of the script whose identity starts %s, not of this one, "
                   "whose identity starts %s",
                   theirs, ours);
    expect_refused_restore(running.script, snapshot, NULL, 0, STASIS_WRONG_SCRIPT, message);
    free_snapshot(snapshot);
    ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_OK);
    ASSERT_INT_EQ(stasis_script_destroy(other), STASIS_OK);
    stop(running);
}

/* The same script loaded again, or stripped of its custom sections, is the same script, so its
 * snapshots restore into either. */
TEST(a_snapshot_restores_into_the_same_script_loaded_again) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "identity");
    stasis_script *stripped = load_script(library, &test_calls_interface, "identity_stripped");
    stasis_instance *instance = start_instance(script);
    const struct snapshot snapshot = take_snapshot(instance);
    stasis_instance *restored = restore(stripped, snapshot, NULL, 0);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_snapshot(snapshot);
    ASSERT_INT_EQ(stasis_script_destroy(stripped), STASIS_OK);
    destroy_all(instance, script, library);
}

/* A snapshot in a format newer than this release knows is refused as too new, and one whose
 * version is 0, which no release writes, as malformed. */
TEST(a_snapshot_in_a_newer_format_is_refused) {
    const struct running running = start_snapshots();
    const struct snapshot snapshot = take_snapshot(running.instance);
    struct snapshot newer = copy(snapshot);
    put_u32(newer.bytes + VERSION_AT, 2);
    expect_refused_restore(running.script, newer, NULL, 0, STASIS_SNAPSHOT_TOO_NEW,
                           "the snapshot's format is version 2, and this release of Stasis reads "
                           "versions up to 1");
    put_u32(newer.bytes + VERSION_AT, 0xFFFFFFFFU);
    expect_refused_restore(running.script, newer, NULL, 0, STASIS_SNAPSHOT_TOO_NEW,
                           "the snapshot's format is version 4294967295, and this release of "
                           "Stasis reads versions up to 1");
    put_u32(newer.bytes + VERSION_AT, 0);
    expect_malformed(running.script, newer,
                     "the snapshot's format is version 0, which no release of Stasis writes");
    free_snapshot(snapshot);
    stop(running);
}

/* Bytes that don't start with a snapshot's magic number aren't a snapshot, and nor are no bytes at
 * all. */
TEST(a_snapshot_with_another_magic_number_is_refused) {
    const struct running running = start_snapshots();
    const struct snapshot snapshot = take_snapshot(running.instance);
    for (size_t i = 0; i < VERSION_AT; i++) {
        struct snapshot other = copy(snapshot);
        other.bytes[i] ^= 0x20;
        expect_malformed(running.script, other,
                         "the bytes don't start with a snapshot's magic number");
    }
    const struct snapshot empty = {NULL, 0};
    expect_refused_restore(running.script, empty, NULL, 0, STASIS_INVALID_SNAPSHOT,
                           "the bytes don't start with a snapshot's magic number");
    free_snapshot(snapshot);
    stop(running);
}

/* A snapshot's memory must be a whole number of pages, from the number the script's memory starts
 * with to the most it can grow to: four pages for snapshots.wat, and for a script whose memory has
 * no maximum, the 65,535 pages an instance may have. */
TEST(a_snapshot_whose_memory_doesnt_fit_the_script_is_refused) {
    const struct running running = start_snapshots();
    const struct snapshot snapshot = take_snapshot(running.instance);
    const size_t memory_at = snapshot.size - PAGE_SIZE;

    struct snapshot uneven = splice(snapshot, memory_at, memory_at, 1);
    put_u64(uneven.bytes + MEMORY_SIZE_AT, 65537);
    expect_malformed(running.script, uneven,
                     "the snapshot's memory is 65537 bytes, which isn't a whole number of pages "
                     "of 65536 bytes");
    struct snapshot none = splice(snapshot, memory_at, snapshot.size, 0);
    put_u64(none.bytes + MEMORY_SIZE_AT, 0);
    expect_malformed(running.script, none,
                     "the snapshot's memory is 0 pages, where the script's memory has from 1 to 4");
    struct snapshot large = splice(snapshot, memory_at, memory_at, (size_t)4 * PAGE_SIZE);
    put_u64(large.bytes + MEMORY_SIZE_AT, (uint64_t)5 * PAGE_SIZE);
    expect_malformed(running.script, large,
                     "the snapshot's memory is 5 pages, where the script's memory has from 1 to 4");
    struct snapshot huge = copy(snapshot);
    put_u64(huge.bytes + MEMORY_SIZE_AT, 0xFFFFFFFFFFFF0000U);
    expect_malformed(running.script, huge,
                     "the snapshot's memory is 281474976710655 pages, where the script's memory "
                     "has from 1 to 4");
    free_snapshot(snapshot);
    stop(running);

    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_calls_interface, "calls");
    stasis_instance *instance = start_instance(script);
    struct snapshot unlimited = take_snapshot(instance);
    put_u64(unlimited.bytes + MEMORY_SIZE_AT, (uint64_t)65536 * PAGE_SIZE);
    expect_malformed(script, unlimited,
                     "the snapshot's memory is 65536 pages, where the script's memory has from 1 "
                     "to 65535");
    destroy_all(instance, script, library);
}

/* A snapshot's memory must be as long as the snapshot says, and nothing may follow it. */
TEST(a_snapshot_of_the_wrong_length_is_refused) {
    const struct running running = start_snapshots();
    const struct snapshot snapshot = take_snapshot(running.instance);
    char message[256];
    struct snapshot longer = splice(snapshot, snapshot.size, snapshot.size, 1);
    (void)snprintf(message, sizeof message,
                   "the snapshot is %zu bytes long, where the length of its memory makes it %zu",
                   snapshot.size + 1, snapshot.size);
    expect_malformed(running.script, longer, message);
    struct snapshot shorter = splice(snapshot, snapshot.size - 1, snapshot.size, 0);
    (void)snprintf(message, sizeof message,
                   "the snapshot is %zu bytes long, where the length of its memory makes it %zu",
                   snapshot.size - 1, snapshot.size);
    expect_malformed(running.script, shorter, message);
    free_snapshot(snapshot);
    stop(running);
}

/* A snapshot's globals must be the script's mutable globals, as many as it defines, each with its
 * index and of its type. */
TEST(a_snapshot_whose_globals_arent_the_scripts_is_refused) {
    const struct running running = start_snapshots();
    const struct snapshot snapshot = take_snapshot(running.instance);
    ASSERT_UINT_EQ(get_u32(snapshot.bytes + GLOBALS_AT), 5);

    struct snapshot fewer = splice(snapshot, global_at(snapshot, 4), global_at(snapshot, 5), 0);
    put_u32(fewer.bytes + GLOBALS_AT, 4);
    expect_malformed(running.script, fewer,
                     "the snapshot holds 4 mutable globals, where the script defines 5");
    struct snapshot more = copy(snapshot);
    put_u32(more.bytes + GLOBALS_AT, 6);
    expect_malformed(running.script, more,
                     "the snapshot holds 6 mutable globals, where the script defines 5");

    /* The immutable global 1 lies between the mutable globals 0 and 2. */
    struct snapshot renumbered = copy(snapshot);
    put_u32(renumbered.bytes + global_at(snapshot, 1), 1);
    expect_malformed(running.script, renumbered,
                     "the snapshot's mutable global 1 is global 1, where the script's is global 2");
    struct snapshot retyped = copy(snapshot);
    retyped.bytes[global_at(snapshot, 2) + 4] = 0x7c;
    expect_malformed(running.script, retyped,
                     "the snapshot's global 3 is of type f64, where the script's is of type i64");
    struct snapshot untyped = copy(snapshot);
    untyped.bytes[global_at(snapshot, 3) + 4] = 0x40;
    expect_malformed(running.script, untyped,
                     "the snapshot's global 4 is of type ?, where the script's is of type f32");
    free_snapshot(snapshot);
    stop(running);
}

/* A snapshot's handle table must record a next handle value that an instance can reach, no more
 * live handles than it records as issued, and each live handle below the next value, in increasing
 * order of value, and of a kind of host object of the interface. */
TEST(a_snapshot_whose_handles_are_out_of_range_is_refused) {
    const struct running running = start_snapshots();
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    ASSERT_UINT_EQ(find(running.instance, 1), 2);
    const struct snapshot snapshot = take_snapshot(running.instance);
    const size_t table = handles_at(snapshot);

    struct snapshot no_next = copy(snapshot);
    put_u64(no_next.bytes + table, 0);
    expect_malformed(running.script, no_next,
                     "the snapshot's next handle value is 0, where it can be from 1 to 4294967296");
    struct snapshot past_next = copy(snapshot);
    put_u64(past_next.bytes + table, 4294967297U);
    expect_malformed(running.script, past_next,
                     "the snapshot's next handle value is 4294967297, where it can be from 1 to "
                     "4294967296");
    struct snapshot crowded = copy(snapshot);
    put_u32(crowded.bytes + table + 8, 3);
    expect_malformed(running.script, crowded,
                     "the snapshot holds 3 live handles, more than the 2 it records as issued");
    struct snapshot zero = copy(snapshot);
    put_u32(zero.bytes + handle_at(snapshot, 0), 0);
    expect_malformed(running.script, zero,
                     "the snapshot's live handle 0 is outside the values it records as issued, "
                     "from 1 to 2");
    struct snapshot unissued = copy(snapshot);
    put_u32(unissued.bytes + handle_at(snapshot, 1), 3);
    expect_malformed(running.script, unissued,
                     "the snapshot's live handle 3 is outside the values it records as issued, "
                     "from 1 to 2");
    struct snapshot unordered = copy(snapshot);
    put_u32(unordered.bytes + handle_at(snapshot, 1), 1);
    expect_malformed(running.script, unordered,
                     "the snapshot's live handle 1 follows handle 1, out of increasing order of "
                     "value");
    struct snapshot unknown = copy(snapshot);
    const size_t name_at = handle_at(snapshot, 1) + 8;
    ASSERT_MEM_EQ(unknown.bytes + name_at, "world.entity", 12);
    unknown.bytes[name_at + 11] = 'z';
    expect_malformed(running.script, unknown,
                     "the snapshot's live handle 2 is of the kind `world.entitz`, which "
                     "`test:snapshots@1.0.0` lacks");
    struct snapshot unnamed = copy(snapshot);
    put_u32(unnamed.bytes + handle_at(snapshot, 1) + 4, 0xFFFFFFFFU);
    expect_malformed(running.script, unnamed, "the snapshot is cut short in its handles");
    free_snapshot(snapshot);
    stop(running);
}

/* A snapshot cut short anywhere is malformed. */
TEST(a_snapshot_cut_short_is_refused) {
    const struct running running = start_snapshots();
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    const struct snapshot snapshot = take_snapshot(running.instance);
    for (size_t size = 0; size < snapshot.size; size++) {
        const struct snapshot cut = {snapshot.bytes, size};
        char message[512];
        stasis_instance *restored = NULL;
        ASSERT_INT_EQ(stasis_instance_restore(running.script, cut.bytes, cut.size, NULL, 0, NULL,
                                              message, sizeof message, &restored),
                      STASIS_INVALID_SNAPSHOT);
        ASSERT_TRUE(restored == NULL);
    }
    const struct snapshot in_version = {snapshot.bytes, VERSION_AT + 2};
    expect_refused_restore(running.script, in_version, NULL, 0, STASIS_INVALID_SNAPSHOT,
                           "the snapshot is cut short in its format's version");
    const struct snapshot in_identity = {snapshot.bytes, IDENTITY_AT + 31};
    expect_refused_restore(running.script, in_identity, NULL, 0, STASIS_INVALID_SNAPSHOT,
                           "the snapshot is cut short in its script's identity");
    const struct snapshot in_memory_size = {snapshot.bytes, MEMORY_SIZE_AT + 7};
    expect_refused_restore(running.script, in_memory_size, NULL, 0, STASIS_INVALID_SNAPSHOT,
                           "the snapshot is cut short in its memory's length");
    const struct snapshot in_globals = {snapshot.bytes, global_at(snapshot, 4) + 6};
    expect_refused_restore(running.script, in_globals, NULL, 0, STASIS_INVALID_SNAPSHOT,
                           "the snapshot is cut short in its globals");
    const struct snapshot in_handles = {snapshot.bytes, handle_at(snapshot, 0) + 9};
    expect_refused_restore(running.script, in_handles, NULL, 0, STASIS_INVALID_SNAPSHOT,
                           "the snapshot is cut short in its handles");
    free_snapshot(snapshot);
    stop(running);
}

/* Whatever a snapshot holds before its memory, a restore of it can only fail with one of the
 * restore's codes, or succeed, and never ends the process, which the sanitize preset checks. Each
 * byte is changed in turn, in three ways. */
TEST(no_change_to_a_snapshot_crashes_a_restore) {
    const struct running running = start_snapshots();
    ASSERT_UINT_EQ(find(running.instance, 0), 1);
    ASSERT_UINT_EQ(pick_up(running.instance, 0), 2);
    const struct snapshot snapshot = take_snapshot(running.instance);
    const stasis_handle bound[2] = {
        {1, TEST_SNAPSHOTS_WORLD_ENTITY_KIND, &entities[1]},
        {2, TEST_SNAPSHOTS_WORLD_ITEM_KIND, &items[1]},
    };
    const unsigned char changes[3] = {0x01, 0x80, 0xFF};
    struct snapshot changed = copy(snapshot);
    for (size_t at = 0; at < snapshot.size - PAGE_SIZE; at++) {
        for (size_t i = 0; i < sizeof changes; i++) {
            changed.bytes[at] = snapshot.bytes[at] ^ changes[i];
            char message[512];
            stasis_instance *restored = NULL;
            const stasis_status status =
                stasis_instance_restore(running.script, changed.bytes, changed.size, bound, 2, NULL,
                                        message, sizeof message, &restored);
            ASSERT_TRUE(status == STASIS_OK || status == STASIS_INVALID_SNAPSHOT ||
                        status == STASIS_WRONG_SCRIPT || status == STASIS_SNAPSHOT_TOO_NEW ||
                        status == STASIS_INVALID_BINDINGS);
            ASSERT_TRUE((status == STASIS_OK) == (restored != NULL));
            ASSERT_TRUE((status == STASIS_OK) == (message[0] == '\0'));
            ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
        }
        changed.bytes[at] = snapshot.bytes[at];
    }
    free_snapshot(changed);
    free_snapshot(snapshot);
    stop(running);
}

/* A snapshot with live handles of both kinds, with the instance it was taken of. */
struct snapshot_with_handles {
    struct running running;
    struct snapshot snapshot;
};

/* Takes a snapshot of an instance given an entity and an item, handles 1 and 2, and then another
 * entity, handle 3. */
static struct snapshot_with_handles snapshot_three_handles(void) {
    struct snapshot_with_handles handled;
    handled.running = start_snapshots();
    ASSERT_UINT_EQ(find(handled.running.instance, 0), 1);
    ASSERT_UINT_EQ(pick_up(handled.running.instance, 0), 2);
    ASSERT_UINT_EQ(find(handled.running.instance, 1), 3);
    handled.snapshot = take_snapshot(handled.running.instance);
    return handled;
}

/* Frees the snapshot, and destroys the instance, its script and its library. */
static void free_handled(struct snapshot_with_handles handled) {
    free_snapshot(handled.snapshot);
    stop(handled.running);
}

/* Checks that restoring the snapshot, binding the `count` handles at `handles`, fails with
 * STASIS_INVALID_BINDINGS and `message`. */
static void expect_invalid_bindings(struct snapshot_with_handles handled,
                                    const stasis_handle *handles, size_t count,
                                    const char *message) {
    expect_refused_restore(handled.running.script, handled.snapshot, handles, count,
                           STASIS_INVALID_BINDINGS, message);
}

/* The kinds of host object, by shorter names. */
enum {
    ENTITY = TEST_SNAPSHOTS_WORLD_ENTITY_KIND,
    ITEM = TEST_SNAPSHOTS_WORLD_ITEM_KIND,
};

/* Every live handle must be bound or revoked, so a restore that leaves one out fails, and creates
 * nothing. */
TEST(a_restore_that_leaves_out_a_handle_is_refused) {
    const struct snapshot_with_handles handled = snapshot_three_handles();
    const stasis_handle handles[2] = {{3, ENTITY, &entities[3]}, {1, ENTITY, NULL}};
    expect_invalid_bindings(handled, handles, 2,
                            "the handles leave out handle 2, of the kind `world.item`");
    expect_invalid_bindings(handled, NULL, 0,
                            "the handles leave out handle 1, of the kind `world.entity`");
    free_handled(handled);
}

/* A handle given twice is refused, even when both entries agree. */
TEST(a_restore_that_gives_a_handle_twice_is_refused) {
    const struct snapshot_with_handles handled = snapshot_three_handles();
    const stasis_handle handles[4] = {
        {1, ENTITY, &entities[0]},
        {2, ITEM, &items[0]},
        {3, ENTITY, &entities[1]},
        {2, ITEM, &items[0]},
    };
    expect_invalid_bindings(handled, handles, 4,
                            "the handles give handle 2, of the kind `world.item`, twice");
    free_handled(handled);
}

/* A handle must be given a host object of its own kind, which the interface has. */
TEST(a_restore_that_gives_a_handle_another_kind_is_refused) {
    const struct snapshot_with_handles handled = snapshot_three_handles();
    stasis_handle handles[3] = {
        {1, ENTITY, &entities[0]},
        {2, ENTITY, &entities[2]},
        {3, ENTITY, &entities[1]},
    };
    expect_invalid_bindings(handled, handles, 3,
                            "the handles give handle 2, of the kind `world.item`, a host object "
                            "of the kind `world.entity`");
    handles[1].kind = 99;
    expect_invalid_bindings(handled, handles, 3,
                            "the handles give handle 2, of the kind `world.item`, a host object "
                            "of kind 99, which the interface lacks");
    free_handled(handled);
}

/* Only the snapshot's live handles can be bound: neither one that was revoked before it was taken,
 * nor one that was never issued. */
TEST(a_restore_that_gives_a_handle_that_isnt_live_is_refused) {
    struct snapshot_with_handles handled = snapshot_three_handles();
    stasis_handle handles[4] = {
        {1, ENTITY, &entities[0]},
        {2, ITEM, &items[0]},
        {3, ENTITY, &entities[1]},
        {4, ENTITY, &entities[2]},
    };
    expect_invalid_bindings(handled, handles, 4,
                            "the handles give handle 4, which isn't live in the snapshot");
    free_snapshot(handled.snapshot);
    ASSERT_INT_EQ(stasis_instance_revoke(handled.running.instance, ENTITY, &entities[0]),
                  STASIS_OK);
    handled.snapshot = take_snapshot(handled.running.instance);
    expect_invalid_bindings(handled, handles, 3,
                            "the handles give handle 1, which isn't live in the snapshot");
    free_handled(handled);
}

/* An instance names a host object by one handle, so a restore can't bind two handles to one host
 * object, though a pointer bound as two kinds is two host objects. */
TEST(a_restore_that_binds_two_handles_to_one_host_object_is_refused) {
    const struct snapshot_with_handles handled = snapshot_three_handles();
    stasis_handle handles[3] = {
        {1, ENTITY, &entities[5]},
        {2, ITEM, &items[5]},
        {3, ENTITY, &entities[5]},
    };
    expect_invalid_bindings(handled, handles, 3,
                            "the handles give handles 1 and 3, of the kind `world.entity`, the "
                            "same host object");
    handles[1].host_object = &entities[5];
    handles[2].host_object = &entities[6];
    stasis_instance *restored = restore(handled.running.script, handled.snapshot, handles, 3);
    ASSERT_UINT_EQ(find(restored, 5), 1);
    ASSERT_UINT_EQ(find(restored, 6), 3);
    ASSERT_INT_EQ(stasis_instance_destroy(restored), STASIS_OK);
    free_handled(handled);
}

TEST_LIST(TEST_CASE(a_restored_instance_carries_on_with_the_memory_it_had),
          TEST_CASE(a_restored_instance_is_ready_without_starting),
          TEST_CASE(a_restored_instance_carries_on_with_the_globals_it_had),
          TEST_CASE(memory_that_grew_between_snapshots_restores_at_its_size),
          TEST_CASE(a_restored_instance_returns_the_strings_it_kept),
          TEST_CASE(the_same_state_gives_the_same_snapshot),
          TEST_CASE(an_instance_lists_its_live_handles_in_order),
          TEST_CASE(a_restore_binds_handles_to_new_host_objects),
          TEST_CASE(a_handle_never_issued_is_forged_after_a_restore),
          TEST_CASE(an_unfinished_line_of_output_travels_in_a_snapshot),
          TEST_CASE(a_c_script_restores), TEST_CASE(a_script_that_isnt_snapshottable_is_refused),
          TEST_CASE(only_a_ready_instance_is_snapshotted),
          TEST_CASE(a_running_instance_isnt_snapshotted), TEST_CASE(a_buffer_too_small_is_refused),
          TEST_CASE(a_buffer_larger_than_the_snapshot_holds_it), TEST_CASE(arguments_are_checked),
          TEST_CASE(a_restored_instance_has_its_own_user_data),
          TEST_CASE(a_restored_instance_belongs_to_its_script),
          TEST_CASE(a_snapshot_of_another_script_is_refused),
          TEST_CASE(a_snapshot_restores_into_the_same_script_loaded_again),
          TEST_CASE(a_snapshot_in_a_newer_format_is_refused),
          TEST_CASE(a_snapshot_with_another_magic_number_is_refused),
          TEST_CASE(a_snapshot_whose_memory_doesnt_fit_the_script_is_refused),
          TEST_CASE(a_snapshot_of_the_wrong_length_is_refused),
          TEST_CASE(a_snapshot_whose_globals_arent_the_scripts_is_refused),
          TEST_CASE(a_snapshot_whose_handles_are_out_of_range_is_refused),
          TEST_CASE(a_snapshot_cut_short_is_refused),
          TEST_CASE(no_change_to_a_snapshot_crashes_a_restore),
          TEST_CASE(a_restore_that_leaves_out_a_handle_is_refused),
          TEST_CASE(a_restore_that_gives_a_handle_twice_is_refused),
          TEST_CASE(a_restore_that_gives_a_handle_another_kind_is_refused),
          TEST_CASE(a_restore_that_gives_a_handle_that_isnt_live_is_refused),
          TEST_CASE(a_restore_that_binds_two_handles_to_one_host_object_is_refused));
