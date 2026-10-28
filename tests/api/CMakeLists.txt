# The CTest tests of the C API. Each program drives Stasis's public functions with WAT scripts built
# from source, and calls their entry points through the typed callers of a test interface's host
# side, which stasis_generate writes:
#
#   libraries.*      creating and destroying libraries, several at once included
#   load.*           loading scripts, the load check, and what a loaded script answers
#   snapshottable.*  whether a loaded script is snapshottable, and why not
#   instances.*      creating, starting, calling and destroying script instances
#   memory.*         an instance's linear memory, zeroed when it's created and when it grows
#   scalars.*        every scalar type, crossing to entry points and back
#
# Every script is a WAT module in scripts/, apart from one C script, and each says what it's for.
# Their names carry the hashes of the test interfaces' signatures, as `stasis-gen signatures` prints
# them.

stasis_generate(host calls.stasis OUTPUT_DIRECTORY generated VARIABLE calls_host)
stasis_generate(host early.stasis OUTPUT_DIRECTORY generated VARIABLE early_host)
stasis_generate(host memory.stasis OUTPUT_DIRECTORY generated VARIABLE memory_host)

# The host sides, compiled as a host's compiler sees them: as C99, with Stasis's own warnings but
# not clang-tidy, which is for Stasis's own code.
add_library(stasis_test_interfaces OBJECT ${calls_host} ${early_host} ${memory_host})
target_include_directories(stasis_test_interfaces PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(stasis_test_interfaces PUBLIC stasis::stasis)
set_target_properties(stasis_test_interfaces PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_CLANG_TIDY ""
)
stasis_target_warnings(stasis_test_interfaces)

set(scripts
    backend_exports
    bad_names
    broken_rules
    calls
    changes_tables
    early
    early_other_minor
    foreign_imports
    identity
    instructions
    memory
    memory_too_large
    mismatches
    missing_required
    newer_minor
    next_major
    no_memory
    non_function_imports
    older_minor
    other_interface
    other_interface_import
    other_signature
    reference_globals
    segments_dont_fit
    segments_fit
    shared_memory
    start_section
    two_memories
    unexported_global
    unexported_memory
    unsnapshottable
    wrong_types
)
add_custom_target(stasis_api_scripts)
foreach(script IN LISTS scripts)
    stasis_add_wat_module(${script} scripts/${script}.wat)
    add_dependencies(stasis_api_scripts stasis_script_${script})
endforeach()

# A C script, built as a script author builds one, whose code is a compiler's.
stasis_add_c_script(c_script scripts/c_script.c)
add_dependencies(stasis_api_scripts stasis_script_c_script)

# A copy of identity.wasm without its custom sections, which is the same script.
set(stripped "${STASIS_SCRIPTS_DIR}/identity_stripped.wasm")
add_custom_command(OUTPUT "${stripped}"
    COMMAND "${STASIS_WASM_TOOLS}" strip --all "${STASIS_SCRIPTS_DIR}/identity.wasm" -o "${stripped}"
    DEPENDS "${STASIS_SCRIPTS_DIR}/identity.wasm"
    COMMENT "Stripping identity.wasm of its custom sections"
    VERBATIM
)
add_custom_target(stasis_script_identity_stripped DEPENDS "${stripped}")
add_dependencies(stasis_api_scripts stasis_script_identity_stripped)

# What the programs share, which reads the scripts from where the build makes them.
add_library(stasis_api_support STATIC support.c support.h)
target_link_libraries(stasis_api_support PUBLIC stasis_harness stasis::stasis stasis_test_interfaces)
target_compile_definitions(stasis_api_support PRIVATE STASIS_SCRIPTS_DIR="${STASIS_SCRIPTS_DIR}")
add_dependencies(stasis_api_support stasis_api_scripts)
stasis_target_conventions(stasis_api_support)

foreach(program IN ITEMS libraries load snapshottable instances memory scalars)
    stasis_add_test(${program} ${program}_test.c)
    target_link_libraries(stasis_test_${program} PRIVATE stasis_api_support)
endforeach()

# The load tests work out a script's identity apart from the library, with SHA-256 alone.
target_link_libraries(stasis_test_load PRIVATE stasis_sha256)
