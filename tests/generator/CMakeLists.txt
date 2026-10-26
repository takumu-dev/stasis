# The CTest tests generator.*, which run stasis-gen and check how it exits and what it prints:
#
#   generator.accepts.<name>     check finds no error in valid/<name>.stasis, and prints nothing
#   generator.signatures.<name>  signatures prints exactly valid/<name>.signatures for it
#   generator.rejects.<name>     check fails on invalid/<name>.stasis, printing exactly the
#                                diagnostics that the file's own //! lines give
#   generator.<what it does>     the command line itself
#
# stasis-gen runs in the interface's directory and is given the file's name alone, so the path in
# each diagnostic is the bare file name.
#
# The hashes in the .signatures files name scripts' imports and entry points, so they can never
# change. Each was computed apart from Stasis, with `printf '%s' '<encoding>' | sha256sum`, and
# acme_dungeon.signatures holds the examples the encoding was specified with.

set(generator "$<TARGET_FILE:stasis_generator>")
set(expect_output "${CMAKE_CURRENT_SOURCE_DIR}/expect_output.cmake")
set(expect_lines "${PROJECT_SOURCE_DIR}/tests/harness/expect.cmake")

# Adds generator.<name>, which runs stasis-gen with the arguments after ARGS in the directory of
# valid interfaces. It expects stasis-gen to exit with EXIT_CODE and to print the lines that LINES
# gives, as tests/harness/expect.cmake checks them.
function(stasis_add_generator_check name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "EXIT_CODE" "ARGS;LINES")
    add_test(NAME generator.${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DCOMMAND=${generator};${arg_ARGS}"
            "-DEXIT_CODE=${arg_EXIT_CODE}"
            "-DLINES=${arg_LINES}"
            -P "${expect_lines}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/valid"
    )
endfunction()

string(REPLACE "." "\\." version "${PROJECT_VERSION}")
set(usage
    [[usage: stasis-gen <command> <interface file>]]
    [[       stasis-gen --version]]
    [[commands:]]
    [[  check       check the interface file, and write nothing]]
    [[  signatures  print the signature encoding and hash of each host function, method and entry]]
    [[              point]]
)

stasis_add_generator_check(prints_its_version ARGS --version EXIT_CODE 0
    LINES "stasis-gen ${version}"
)
stasis_add_generator_check(prints_its_usage ARGS --help EXIT_CODE 0 LINES ${usage})
stasis_add_generator_check(refuses_no_command EXIT_CODE 2 LINES ${usage})
stasis_add_generator_check(refuses_an_unknown_command ARGS frobnicate dungeon.stasis
    EXIT_CODE 2
    LINES "stasis-gen: there's no command `frobnicate`" ${usage}
)
stasis_add_generator_check(refuses_a_command_without_an_interface ARGS check EXIT_CODE 2
    LINES ${usage}
)
stasis_add_generator_check(refuses_two_interfaces ARGS check dungeon.stasis dungeon.stasis
    EXIT_CODE 2
    LINES ${usage}
)
stasis_add_generator_check(refuses_an_interface_it_cant_read ARGS check missing.stasis
    EXIT_CODE 2
    LINES "stasis-gen: can't read missing\\.stasis: .+"
)

# Adds the CTest test generator.<name>, which runs stasis-gen with the arguments after ARGS, in the
# directory of interfaces DIRECTORY names, valid or invalid. As expect_output.cmake checks, it
# expects stasis-gen to exit with EXIT_CODE, to print exactly what the file STDOUT holds, if given,
# and nothing otherwise, and to print exactly the diagnostics in the //! lines of the file
# DIAGNOSTICS, if given, and nothing otherwise.
function(stasis_add_generator_output_check name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "DIRECTORY;EXIT_CODE;STDOUT;DIAGNOSTICS" "ARGS")
    set(expected "")
    if(arg_STDOUT)
        list(APPEND expected "-DSTDOUT=${arg_STDOUT}")
    endif()
    if(arg_DIAGNOSTICS)
        list(APPEND expected "-DDIAGNOSTICS=${arg_DIAGNOSTICS}")
    endif()
    add_test(NAME generator.${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DCOMMAND=${generator};${arg_ARGS}"
            "-DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/${arg_DIRECTORY}"
            "-DEXIT_CODE=${arg_EXIT_CODE}"
            ${expected}
            -P "${expect_output}"
    )
endfunction()

# Like check, signatures prints only the diagnostics of an interface with errors.
stasis_add_generator_output_check(signatures_prints_nothing_for_an_invalid_interface
    DIRECTORY invalid
    ARGS signatures no_script.stasis
    EXIT_CODE 1
    DIAGNOSTICS "${CMAKE_CURRENT_SOURCE_DIR}/invalid/no_script.stasis"
)

file(GLOB valid_interfaces CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/valid/*.stasis")
foreach(interface IN LISTS valid_interfaces)
    cmake_path(GET interface FILENAME file)
    cmake_path(GET interface STEM name)
    stasis_add_generator_output_check(accepts.${name} DIRECTORY valid ARGS check ${file} EXIT_CODE 0)
endforeach()

file(GLOB signatures CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/valid/*.signatures")
foreach(expected IN LISTS signatures)
    cmake_path(GET expected STEM name)
    stasis_add_generator_output_check(signatures.${name}
        DIRECTORY valid
        ARGS signatures ${name}.stasis
        EXIT_CODE 0
        STDOUT "${expected}"
    )
endforeach()

file(GLOB invalid_interfaces CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/invalid/*.stasis")
foreach(interface IN LISTS invalid_interfaces)
    cmake_path(GET interface FILENAME file)
    cmake_path(GET interface STEM name)
    stasis_add_generator_output_check(rejects.${name}
        DIRECTORY invalid
        ARGS check ${file}
        EXIT_CODE 1
        DIAGNOSTICS "${interface}"
    )
endforeach()
