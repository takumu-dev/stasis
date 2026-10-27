# The CTest tests generator.*, which run stasis-gen and check how it exits and what it prints:
#
#   generator.accepts.<name>     check finds no error in valid/<name>.stasis, and prints nothing
#   generator.signatures.<name>  signatures prints exactly valid/<name>.signatures for it
#   generator.rejects.<name>     check fails on invalid/<name>.stasis, printing exactly the
#                                diagnostics that the file's own //! lines give
#   generator.host.<name>        host writes exactly the files in host/<name>/, the golden host
#                                side of valid/<name>.stasis, or for crlf_prefixed, of crlf.stasis
#                                with a prefix of its own
#   generator.<what it does>     the command line itself
#   generator.stasis_generate.*  the CMake function that runs stasis-gen in a build: in this build,
#                                and in a host's build that names its own stasis-gen
#
# The golden host sides are rewritten on purpose, by building the target rewrite-generator-goldens,
# and never by a test.
#
# The build compiles each golden host side as a host's compiler sees it: its header on its own as
# C99, as C++11 and as the newest C++, and its source as C99, all pedantic and with Stasis's own
# warnings. The host side of escaping.stasis is compiled again with the lowercase macros it
# escapes defined.
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
    [[usage: stasis-gen <command> \[<option>\.\.\.\] <interface file>]]
    [[       stasis-gen --version]]
    [[commands:]]
    [[  check       check the interface file, and write nothing]]
    [[  host        write the host's side of the interface, as a C header and source]]
    [[  signatures  print the signature encoding and hash of each host function, method and entry]]
    [[              point]]
    [[options of host:]]
    [[  --prefix <prefix>         start every C name with <prefix>, not <namespace>_<name>_]]
    [[  --output-directory <dir>  write into <dir>, not the current directory]]
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
# DIAGNOSTICS, if given, and nothing otherwise. If EMPTY_DIRECTORY is given, stasis-gen must leave
# that directory empty.
function(stasis_add_generator_output_check name)
    cmake_parse_arguments(PARSE_ARGV 1 arg ""
        "DIRECTORY;EXIT_CODE;STDOUT;DIAGNOSTICS;EMPTY_DIRECTORY" "ARGS"
    )
    set(expected "")
    if(arg_STDOUT)
        list(APPEND expected "-DSTDOUT=${arg_STDOUT}")
    endif()
    if(arg_DIAGNOSTICS)
        list(APPEND expected "-DDIAGNOSTICS=${arg_DIAGNOSTICS}")
    endif()
    if(arg_EMPTY_DIRECTORY)
        list(APPEND expected "-DEMPTY_DIRECTORY=${arg_EMPTY_DIRECTORY}")
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

# Like check, signatures prints only the diagnostics of an interface with errors, and host prints
# them and writes nothing. Every command reports C names that clash.
stasis_add_generator_output_check(signatures_prints_nothing_for_an_invalid_interface
    DIRECTORY invalid
    ARGS signatures no_script.stasis
    EXIT_CODE 1
    DIAGNOSTICS "${CMAKE_CURRENT_SOURCE_DIR}/invalid/no_script.stasis"
)
stasis_add_generator_output_check(signatures_reports_c_name_clashes
    DIRECTORY invalid
    ARGS signatures c_name_clashes.stasis
    EXIT_CODE 1
    DIAGNOSTICS "${CMAKE_CURRENT_SOURCE_DIR}/invalid/c_name_clashes.stasis"
)
set(nothing_written "${CMAKE_CURRENT_BINARY_DIR}/nothing_written")
stasis_add_generator_output_check(host_writes_nothing_for_an_invalid_interface
    DIRECTORY invalid
    ARGS host --output-directory "${nothing_written}" c_name_clashes.stasis
    EXIT_CODE 1
    DIAGNOSTICS "${CMAKE_CURRENT_SOURCE_DIR}/invalid/c_name_clashes.stasis"
    EMPTY_DIRECTORY "${nothing_written}"
)

# host's options, and the files it writes.
stasis_add_generator_check(host_refuses_a_prefix_with_capitals ARGS host --prefix Acme_ crlf.stasis
    EXIT_CODE 2
    LINES "stasis-gen: the prefix `Acme_` isn't lowercase snake_case with a `_` after its last word"
)
stasis_add_generator_check(host_refuses_a_prefix_without_an_underscore
    ARGS host --prefix acme crlf.stasis
    EXIT_CODE 2
    LINES "stasis-gen: the prefix `acme` isn't lowercase snake_case with a `_` after its last word"
)
string(CONCAT reserved_prefix
    "stasis-gen: the prefix `stasis_dungeon_` starts with `stasis_`, which is reserved for "
    "Stasis's own names"
)
stasis_add_generator_check(host_refuses_a_reserved_prefix
    ARGS host --prefix stasis_dungeon_ crlf.stasis
    EXIT_CODE 2
    LINES "${reserved_prefix}"
)
stasis_add_generator_check(check_takes_no_prefix ARGS check --prefix acme_ crlf.stasis EXIT_CODE 2
    LINES "stasis-gen: `check` takes no `--prefix`" ${usage}
)
stasis_add_generator_check(refuses_an_unknown_option ARGS host --frobnicate crlf.stasis
    EXIT_CODE 2
    LINES "stasis-gen: there's no option `--frobnicate`" ${usage}
)
stasis_add_generator_check(refuses_an_option_given_twice
    ARGS host --prefix a_ --prefix b_ crlf.stasis
    EXIT_CODE 2
    LINES "stasis-gen: `--prefix` is given twice" ${usage}
)
stasis_add_generator_check(refuses_an_option_without_a_value
    ARGS host crlf.stasis --output-directory
    EXIT_CODE 2
    LINES "stasis-gen: `--output-directory` needs a value" ${usage}
)
stasis_add_generator_check(host_refuses_a_directory_it_cant_write
    ARGS host --output-directory missing crlf.stasis
    EXIT_CODE 2
    LINES "stasis-gen: can't write missing/test_crlf\\.h: .+"
)
add_test(NAME generator.host_rewrites_only_changed_files
    COMMAND "${CMAKE_COMMAND}"
        "-DGENERATOR=${generator}"
        "-DINTERFACE=${CMAKE_CURRENT_SOURCE_DIR}/valid/crlf.stasis"
        "-DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/rewrites"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_rewrites.cmake"
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

# Adds generator.host.<name>, which checks that host writes exactly the files of the golden host
# side host/<name>/ for `interface`, with the options that follow it, and adds the commands that
# rewrite that golden host side to rewrite-generator-goldens.
set(rewrite_commands "")
function(stasis_add_host_golden name interface)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/host/${name}")
    set(golden "${CMAKE_CURRENT_SOURCE_DIR}/host/${name}")
    add_test(NAME generator.host.${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DCOMMAND=${generator};host;${ARGN};--output-directory;${output};${interface}"
            "-DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/valid"
            "-DOUTPUT_DIRECTORY=${output}"
            "-DEXPECTED=${golden}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_files.cmake"
    )
    list(APPEND rewrite_commands
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${golden}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${golden}"
        COMMAND "${generator}" host ${ARGN} --output-directory "${golden}" "${interface}"
    )
    set(rewrite_commands "${rewrite_commands}" PARENT_SCOPE)
endfunction()

foreach(interface IN LISTS valid_interfaces)
    cmake_path(GET interface FILENAME file)
    cmake_path(GET interface STEM name)
    stasis_add_host_golden(${name} ${file})
endforeach()
stasis_add_host_golden(crlf_prefixed crlf.stasis --prefix my_crlf_)

add_custom_target(rewrite-generator-goldens
    ${rewrite_commands}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/valid"
    COMMENT "Rewriting the generator's golden host sides from what stasis-gen writes now"
    VERBATIM
)
add_dependencies(rewrite-generator-goldens stasis_generator)

file(GLOB golden_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/host/*/*.h")
foreach(header IN LISTS golden_headers)
    stasis_add_header_checks("${header}")
endforeach()

file(GLOB golden_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/host/*/*.c")
foreach(source IN LISTS golden_sources)
    cmake_path(GET source STEM name)
    stasis_add_compile_check(stasis_host_source_check_${name} "${source}" C 99)
endforeach()

set(escaping "${CMAKE_CURRENT_SOURCE_DIR}/host/escaping")
stasis_add_compile_check(stasis_host_macro_check_c escaping_macros.c C 99 "${escaping}")
stasis_add_compile_check(stasis_host_macro_check_cxx escaping_macros.cpp CXX 11 "${escaping}")

# This build generates the host side of the sample dungeon interface with stasis_generate, compiles
# it, and checks it against its golden host side.
stasis_generate(host valid/dungeon.stasis OUTPUT_DIRECTORY generated VARIABLE dungeon_host)
list(GET dungeon_host 1 dungeon_source)
stasis_add_compile_check(stasis_generated_host_side "${dungeon_source}" C 99
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
foreach(file IN LISTS dungeon_host)
    cmake_path(GET file FILENAME name)
    add_test(NAME generator.stasis_generate.writes_${name}
        COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${file}" "${CMAKE_CURRENT_SOURCE_DIR}/host/dungeon/${name}"
    )
endforeach()

# A host's build that doesn't build Stasis, configured and built by each test.
foreach(case IN ITEMS
    uses_stasis_generator
    reruns_when_its_inputs_change
    refuses_another_version
    needs_a_generator
)
    add_test(NAME generator.stasis_generate.${case}
        COMMAND "${CMAKE_COMMAND}"
            "-DCASE=${case}"
            "-DSTASIS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DBUILD_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/consumer/${case}"
            "-DGENERATOR=${generator}"
            "-DVERSION=${PROJECT_VERSION}"
            "-DINTERFACE=${CMAKE_CURRENT_SOURCE_DIR}/valid/crlf.stasis"
            "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/host/crlf"
            "-DCMAKE_GENERATOR=${CMAKE_GENERATOR}"
            "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_consumer.cmake"
    )
endforeach()
