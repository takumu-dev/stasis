/* Calls that trap, and the trap reports they leave: the kind of each trap, its message and its
 * frames, and an instance that refuses everything once it has trapped. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_traps.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A trapped instance has a report, and no other instance has one. */
TEST(reports_nothing_until_it_traps) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_traps_interface, "traps");
    stasis_instance *instance = NULL;
    ASSERT_INT_EQ(stasis_instance_create(script, NULL, &instance), STASIS_OK);
    ASSERT_TRUE(stasis_instance_trap(instance) == NULL);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_OK);
    ASSERT_TRUE(stasis_instance_trap(instance) == NULL);
    ASSERT_INT_EQ(test_traps_trapper_returns(instance), STASIS_OK);
    ASSERT_TRUE(stasis_instance_trap(instance) == NULL);
    ASSERT_TRUE(stasis_instance_trap(NULL) == NULL);
    destroy_all(instance, script, library);
}

TEST(a_trap_leaves_a_report) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_traps_interface, "traps");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_traps_trapper_unreachable(instance), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_UNREACHABLE);
    ASSERT_STR_EQ(trap->message, "unreachable");
    destroy_all(instance, script, library);
}

/* Each later call is refused, even one that would trap in another way, and the report stays as the
 * trap left it. */
TEST(a_trapped_instance_refuses_every_call) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_traps_interface, "traps");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_traps_trapper_out_of_bounds(instance), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_INT_EQ(test_traps_trapper_returns(instance), STASIS_REFUSED_TRAPPED);
    ASSERT_INT_EQ(test_traps_trapper_unreachable(instance), STASIS_REFUSED_TRAPPED);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_REFUSED_TRAPPED);
    ASSERT_TRUE(stasis_instance_trap(instance) == trap);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_MEMORY_OUT_OF_BOUNDS);
    ASSERT_STR_EQ(trap->message, "out of bounds memory access");
    destroy_all(instance, script, library);
}

/* A new instance whose stasis_initialize traps comes back trapped, with its report, and is never
 * ready: its entry points are refused as a trapped instance's, not as one that hasn't started. */
TEST(an_instance_whose_initialisation_traps_never_becomes_ready) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_traps_interface, "initialize_traps");
    stasis_instance *instance = NULL;
    ASSERT_INT_EQ(stasis_instance_create(script, NULL, &instance), STASIS_OK);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_UNREACHABLE);
    ASSERT_STR_EQ(trap->message, "unreachable");
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_REFUSED_TRAPPED);
    ASSERT_INT_EQ(test_traps_trapper_returns(instance), STASIS_REFUSED_TRAPPED);
    ASSERT_TRUE(stasis_instance_trap(instance) == trap);
    destroy_all(instance, script, library);
}

/* Calls `entry_point` in a new instance of traps.wasm, which must trap as `kind`, with WAMR's
 * description of the trap as its message, in the function named `function`. */
static void expect_trap(uint32_t entry_point, stasis_trap_kind kind, const char *message,
                        const char *function) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_traps_interface, "traps");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(stasis_call(instance, &test_traps_interface, entry_point, NULL, NULL),
                  STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_TRUE(trap != NULL);
    ASSERT_INT_EQ(trap->kind, kind);
    ASSERT_STR_EQ(trap->message, message);
    ASSERT_TRUE(trap->frame_count >= 1);
    ASSERT_STR_EQ(trap->frames[0].function_name, function);
    destroy_all(instance, script, library);
}

/* Each kind of WebAssembly's own is mapped from WAMR's description of the trap, so each of WAMR's
 * descriptions that maps to a kind is checked here, with the message it gives. */

TEST(traps_as_unreachable) {
    expect_trap(TEST_TRAPS_TRAPPER_UNREACHABLE_ENTRY, STASIS_TRAP_KIND_UNREACHABLE, "unreachable",
                "unreachable");
}

TEST(traps_as_memory_out_of_bounds) {
    expect_trap(TEST_TRAPS_TRAPPER_OUT_OF_BOUNDS_ENTRY, STASIS_TRAP_KIND_MEMORY_OUT_OF_BOUNDS,
                "out of bounds memory access", "out_of_bounds");
}

TEST(traps_as_bad_indirect_call) {
    expect_trap(TEST_TRAPS_TRAPPER_UNDEFINED_ELEMENT_ENTRY, STASIS_TRAP_KIND_BAD_INDIRECT_CALL,
                "undefined element", "undefined_element");
    expect_trap(TEST_TRAPS_TRAPPER_UNINITIALIZED_ELEMENT_ENTRY, STASIS_TRAP_KIND_BAD_INDIRECT_CALL,
                "uninitialized element", "uninitialized_element");
    expect_trap(TEST_TRAPS_TRAPPER_TYPE_MISMATCH_ENTRY, STASIS_TRAP_KIND_BAD_INDIRECT_CALL,
                "indirect call type mismatch", "type_mismatch");
}

TEST(traps_as_integer_divide_by_zero) {
    expect_trap(TEST_TRAPS_TRAPPER_DIVIDE_BY_ZERO_ENTRY, STASIS_TRAP_KIND_INTEGER_DIVIDE_BY_ZERO,
                "integer divide by zero", "divide_by_zero");
}

TEST(traps_as_integer_overflow) {
    expect_trap(TEST_TRAPS_TRAPPER_DIVISION_OVERFLOW_ENTRY, STASIS_TRAP_KIND_INTEGER_OVERFLOW,
                "integer overflow", "division_overflow");
    expect_trap(TEST_TRAPS_TRAPPER_CONVERSION_OVERFLOW_ENTRY, STASIS_TRAP_KIND_INTEGER_OVERFLOW,
                "integer overflow", "conversion_overflow");
}

TEST(traps_as_invalid_conversion) {
    expect_trap(TEST_TRAPS_TRAPPER_INVALID_CONVERSION_ENTRY, STASIS_TRAP_KIND_INVALID_CONVERSION,
                "invalid conversion to integer", "invalid_conversion");
}

/* Recursing runs out of the interpreter's stack, which holds the instance's frames. */
TEST(traps_as_stack_exhausted) {
    expect_trap(TEST_TRAPS_TRAPPER_RECURSE_ENTRY, STASIS_TRAP_KIND_STACK_EXHAUSTED,
                "wasm operand stack overflow", "recurse");
}

/* A trap that's none of the other kinds, such as reading a table outside its bounds, has WAMR's
 * description too. */
TEST(traps_as_other) {
    expect_trap(TEST_TRAPS_TRAPPER_TABLE_OUT_OF_BOUNDS_ENTRY, STASIS_TRAP_KIND_OTHER,
                "out of bounds table access", "table_out_of_bounds");
}

/* How many frames deep `recurse` gets in an instance of a library with the stack size given, before
 * it traps for lack of stack. */
static size_t recursion_depth(size_t stack_size) {
    stasis_library_settings settings;
    memset(&settings, 0, sizeof settings);
    settings.stack_size = stack_size;
    stasis_library *library = NULL;
    ASSERT_INT_EQ(stasis_library_create(&settings, &library), STASIS_OK);
    stasis_script *script = load_script(library, &test_traps_interface, "traps");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_traps_trapper_recurse(instance), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_INT_EQ(trap->kind, STASIS_TRAP_KIND_STACK_EXHAUSTED);
    for (size_t i = 0; i < trap->frame_count; i++) {
        ASSERT_STR_EQ(trap->frames[i].function_name, "recurse");
    }
    const size_t depth = trap->frame_count;
    destroy_all(instance, script, library);
    return depth;
}

/* The interpreter's stack is the size the library's settings give, so a larger one lets a script
 * recurse deeper. */
TEST(the_stack_size_sets_how_deep_a_script_recurses) {
    const size_t shallow = recursion_depth(16384);
    const size_t deep = recursion_depth(STASIS_DEFAULT_STACK_SIZE);
    ASSERT_TRUE(shallow > 0);
    ASSERT_TRUE(deep > shallow);
}

/* Calls `nested` in a new instance of the script <name>.wasm, which must trap with two frames: the
 * function that trapped and the entry point that called it, the first two functions of traps.wat.
 * Each frame must have the name given, which may be null. */
static void expect_nested_frames(const char *name, const char *inner, const char *outer) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_traps_interface, name);
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_traps_trapper_nested(instance), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_SIZE_EQ(trap->frame_count, 2);
    ASSERT_UINT_EQ(trap->frames[0].function_index, 0);
    ASSERT_STR_EQ(trap->frames[0].function_name, inner);
    ASSERT_UINT_EQ(trap->frames[1].function_index, 1);
    ASSERT_STR_EQ(trap->frames[1].function_name, outer);
    destroy_all(instance, script, library);
}

TEST(frames_name_each_function_innermost_first) {
    expect_nested_frames("traps", "traps", "nested");
}

/* A copy of traps.wasm without its custom sections has no name section, so its frames have no
 * names, not even those of the functions it exports. */
TEST(frames_have_no_names_without_a_name_section) {
    expect_nested_frames("traps_stripped", NULL, NULL);
}

/* Reads the unsigned LEB128 number at `*at`, and moves past it. */
static uint32_t read_leb(const uint8_t **at) {
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        const uint8_t byte = *(*at)++;
        value |= (uint32_t)(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            break;
        }
    }
    return value;
}

/* The offset in `module` of the first instruction of the function at `function`, which must define
 * no locals, in a module that imports no functions. */
static uint32_t first_instruction(struct module_bytes module, uint32_t function) {
    enum { CODE_SECTION = 10 };

    const uint8_t *at = module.bytes + 8;
    for (;;) {
        ASSERT_TRUE(at < module.bytes + module.size);
        const uint8_t id = *at++;
        const uint32_t size = read_leb(&at);
        if (id == CODE_SECTION) {
            break;
        }
        at += size;
    }
    ASSERT_TRUE(function < read_leb(&at));
    for (uint32_t i = 0; i < function; i++) {
        const uint32_t size = read_leb(&at);
        at += size;
    }
    (void)read_leb(&at);
    ASSERT_UINT_EQ(read_leb(&at), 0);
    return (uint32_t)(at - module.bytes);
}

/* The classic interpreter says where in the module each frame's function was: in the innermost
 * frame at the instruction that trapped, and in the one that called it at its call. In each of the
 * two functions, that instruction follows an `i32.const 1000` and a `drop`, which take four bytes.
 * The fast interpreter can't say. */
TEST(frames_say_where_each_function_was) {
    enum { BEFORE = 4 };

    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_traps_interface, "traps");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_traps_trapper_nested(instance), STASIS_TRAP);
    const stasis_trap *trap = stasis_instance_trap(instance);
    ASSERT_SIZE_EQ(trap->frame_count, 2);
    if (STASIS_TEST_CLASSIC_INTERPRETER) {
        const struct module_bytes module = read_module("traps");
        ASSERT_UINT_EQ(trap->frames[0].module_offset, first_instruction(module, 0) + BEFORE);
        ASSERT_UINT_EQ(trap->frames[1].module_offset, first_instruction(module, 1) + BEFORE);
        free_module(module);
    } else {
        ASSERT_UINT_EQ(trap->frames[0].module_offset, STASIS_NO_OFFSET);
        ASSERT_UINT_EQ(trap->frames[1].module_offset, STASIS_NO_OFFSET);
    }
    destroy_all(instance, script, library);
}

TEST_LIST(TEST_CASE(reports_nothing_until_it_traps), TEST_CASE(a_trap_leaves_a_report),
          TEST_CASE(a_trapped_instance_refuses_every_call),
          TEST_CASE(an_instance_whose_initialisation_traps_never_becomes_ready),
          TEST_CASE(traps_as_unreachable), TEST_CASE(traps_as_memory_out_of_bounds),
          TEST_CASE(traps_as_bad_indirect_call), TEST_CASE(traps_as_integer_divide_by_zero),
          TEST_CASE(traps_as_integer_overflow), TEST_CASE(traps_as_invalid_conversion),
          TEST_CASE(traps_as_stack_exhausted), TEST_CASE(traps_as_other),
          TEST_CASE(the_stack_size_sets_how_deep_a_script_recurses),
          TEST_CASE(frames_name_each_function_innermost_first),
          TEST_CASE(frames_have_no_names_without_a_name_section),
          TEST_CASE(frames_say_where_each_function_was));
