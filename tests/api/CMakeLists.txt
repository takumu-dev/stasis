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
#   host_functions.* scripts calling host functions, which the program defines
#   strings.*        strings crossing both ways, and the load check of what that needs
#   allocations.*    what calls with strings allocate, which the test-only library counts
#   traps.*          calls that trap, and the trap reports they leave
#   printing.*       where WAMR's printing goes when a script traps
#
# Every script is a WAT module in scripts/, apart from two C scripts, and each says what it's for.
# Their names carry the hashes of the test interfaces' signatures, as `stasis-gen signatures` prints
# them.

stasis_generate(host calls.stasis OUTPUT_DIRECTORY generated VARIABLE calls_host)
stasis_generate(host early.stasis OUTPUT_DIRECTORY generated VARIABLE early_host)
stasis_generate(host memory.stasis OUTPUT_DIRECTORY generated VARIABLE memory_host)
stasis_generate(host traps.stasis OUTPUT_DIRECTORY generated VARIABLE traps_host)
stasis_generate(host hosts.stasis OUTPUT_DIRECTORY generated VARIABLE hosts_host)
stasis_generate(host strings.stasis OUTPUT_DIRECTORY generated VARIABLE strings_host)

set(scripts
    abi_exports_wrong_types
    backend_exports
    bad_names
    broken_rules
    calls
    changes_tables
    early
    early_other_minor
    foreign_imports
    host_bad_names
    host_mismatches
    host_wrong_types
    hosts
    identity
    initialize_traps
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
    realloc_missing_for_a_result
    realloc_missing_for_an_argument
    realloc_not_a_function
    realloc_not_needed
    reference_globals
    segments_dont_fit
    segments_fit
    shared_memory
    start_section
    strings
    traps
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

# C scripts, built as a script author builds one, whose code is a compiler's.
foreach(script IN ITEMS c_script c_strings)
    stasis_add_c_script(${script} scripts/${script}.c)
    add_dependencies(stasis_api_scripts stasis_script_${script})
endforeach()

# Copies of identity.wasm and traps.wasm without their custom sections, which are the same scripts:
# the first without a section of its own, and the second without its name section.
foreach(script IN ITEMS identity traps)
    set(stripped "${STASIS_SCRIPTS_DIR}/${script}_stripped.wasm")
    add_custom_command(OUTPUT "${stripped}"
        COMMAND "${STASIS_WASM_TOOLS}" strip --all "${STASIS_SCRIPTS_DIR}/${script}.wasm"
            -o "${stripped}"
        DEPENDS "${STASIS_SCRIPTS_DIR}/${script}.wasm"
        COMMENT "Stripping ${script}.wasm of its custom sections"
        VERBATIM
    )
    add_custom_target(stasis_script_${script}_stripped DEPENDS "${stripped}")
    add_dependencies(stasis_api_scripts stasis_script_${script}_stripped)
endforeach()

# Adds the object library `target`, of the host sides that follow, against `library`. They're
# compiled as a host's compiler sees them: as C99, with Stasis's own warnings but not clang-tidy,
# which is for Stasis's own code.
function(stasis_add_host_sides target library)
    add_library(${target} OBJECT ${ARGN})
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(${target} PUBLIC ${library})
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        C_CLANG_TIDY ""
    )
    stasis_target_warnings(${target})
endfunction()

# Adds what the programs share, against `library`, the library or the test-only library, since a
# program can't link both: the host sides, as the object library stasis_test_interfaces<suffix>,
# and support.c, which reads the scripts from where the build makes them and runs threads, as the
# static library stasis_api_support<suffix>.
function(stasis_add_api_support suffix library)
    set(interfaces stasis_test_interfaces${suffix})
    stasis_add_host_sides(${interfaces} ${library}
        ${calls_host} ${early_host} ${memory_host} ${traps_host}
    )

    set(support stasis_api_support${suffix})
    add_library(${support} STATIC support.c support.h)
    target_link_libraries(${support} PUBLIC stasis_harness ${library} ${interfaces})
    # pthread_getattr_np, which finds a thread's stack, is a GNU extension.
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_libraries(${support} PRIVATE Threads::Threads)
        target_compile_definitions(${support} PRIVATE _GNU_SOURCE)
    endif()
    target_compile_definitions(${support} PRIVATE STASIS_SCRIPTS_DIR="${STASIS_SCRIPTS_DIR}")
    add_dependencies(${support} stasis_api_scripts)
    stasis_target_conventions(${support})
endfunction()

stasis_add_api_support("" stasis::stasis)
stasis_add_api_support(_testing stasis_testing)

foreach(program IN ITEMS libraries load snapshottable instances memory scalars traps)
    stasis_add_test(${program} ${program}_test.c)
    target_link_libraries(stasis_test_${program} PRIVATE stasis_api_support)
endforeach()

# The host side of hosts.stasis declares host functions, which only the program that defines them
# links.
stasis_add_host_sides(stasis_test_hosts_interface stasis::stasis ${hosts_host})
stasis_add_test(host_functions host_functions_test.c)
target_link_libraries(stasis_test_host_functions PRIVATE
    stasis_api_support
    stasis_test_hosts_interface
)
# A host function is sure of this much of the stack, which the program checks.
target_compile_definitions(stasis_test_host_functions PRIVATE
    STASIS_HOST_FUNCTION_STACK=${STASIS_HOST_FUNCTION_STACK}
)

# So does the host side of strings.stasis, whose host functions string_hosts.c defines, for the
# program that calls the strings script against the library, and the one that counts what its calls
# allocate, against the test-only library.
stasis_add_host_sides(stasis_test_strings_interface stasis::stasis ${strings_host})
stasis_add_test(strings strings_test.c string_hosts.c string_hosts.h)
target_link_libraries(stasis_test_strings PRIVATE
    stasis_api_support
    stasis_test_strings_interface
)
stasis_add_host_sides(stasis_test_strings_interface_testing stasis_testing ${strings_host})
stasis_add_test(allocations allocations_test.c string_hosts.c string_hosts.h testing.h)
target_link_libraries(stasis_test_allocations PRIVATE
    stasis_api_support_testing
    stasis_test_strings_interface_testing
)

# The load tests work out a script's identity apart from the library, with SHA-256 alone.
target_link_libraries(stasis_test_load PRIVATE stasis_sha256)

# Only the classic interpreter says where in a module each frame of a trap report was.
target_compile_definitions(stasis_test_traps PRIVATE
    STASIS_TEST_CLASSIC_INTERPRETER=$<STREQUAL:${STASIS_WAMR_INTERPRETER},classic>
)

# The printing program, built against each library, and run by a test of each that checks what it
# prints: nothing but the harness's line from the library, and the trap's call stack before it from
# the test-only library.
add_executable(stasis_test_printing printing_test.c)
target_link_libraries(stasis_test_printing PRIVATE stasis_api_support)
stasis_target_conventions(stasis_test_printing)
add_executable(stasis_test_printing_testing printing_test.c)
target_link_libraries(stasis_test_printing_testing PRIVATE stasis_api_support_testing)
stasis_target_conventions(stasis_test_printing_testing)

set(expect "${PROJECT_SOURCE_DIR}/tests/harness/expect.cmake")
add_test(NAME printing.the_library_prints_nothing_when_a_script_traps
    COMMAND "${CMAKE_COMMAND}"
        "-DCOMMAND=$<TARGET_FILE:stasis_test_printing>;traps"
        -DEXIT_CODE=0
        "-DLINES=PASS traps"
        -P "${expect}"
)
set(call_stack "" "#00: 0x[0-9a-f]+ - unreachable" "")
add_test(NAME printing.the_test_only_library_prints_the_call_stack_of_a_trap
    COMMAND "${CMAKE_COMMAND}"
        "-DCOMMAND=$<TARGET_FILE:stasis_test_printing_testing>;traps"
        -DEXIT_CODE=0
        "-DLINES=${call_stack};PASS traps"
        -P "${expect}"
)
