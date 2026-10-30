# The CTest tests generator.*, which run stasis-gen and check how it exits and what it prints:
#
#   generator.accepts.<name>     check finds no error in valid/<name>.stasis, and prints nothing
#   generator.signatures.<name>  signatures prints exactly valid/<name>.signatures for it
#   generator.rejects.<name>     check fails on invalid/<name>.stasis, printing exactly the
#                                diagnostics that the file's own //! lines give
#   generator.host.<name>        host writes exactly the files in host/<name>/, the golden host
#                                side of valid/<name>.stasis, or for crlf_prefixed, of crlf.stasis
#                                with a prefix of its own
#   generator.c.<name>           c writes exactly the files in c/<name>/, the golden C bindings of
#                                valid/<name>.stasis, or for crlf_prefixed, of crlf.stasis with a
#                                prefix of its own
#   generator.c_header_refuses.<what>
#                                a golden C bindings' header refuses to compile for a target that
#                                isn't wasm32-wasip1, or with threads
#   generator.wit.<name>         wit writes exactly the file in wit/<name>/, the golden WIT
#                                rendering of valid/<name>.stasis
#   generator.wasm_tools_reads_wit.<name>
#                                wasm-tools reads the golden WIT rendering wit/<name>/ as a WIT
#                                package, and encodes and validates it
#   generator.<what it does>     the command line itself
#   generator.stasis_generate.*  the CMake function that runs stasis-gen in a build: in this build,
#                                and in a host's build that names its own stasis-gen
#
# The golden host sides, C bindings and WIT renderings are rewritten on purpose, by building the
# target rewrite-generator-goldens, and never by a test.
#
# The build compiles each golden host side as a host's compiler sees it: its header on its own as
# C99, as C++11 and as the newest C++, and its source as C99, all pedantic and with Stasis's own
# warnings. The host side of escaping.stasis is compiled again with the lowercase macros it
# escapes defined. It compiles each golden C bindings as a script author's build does, with the
# pinned wasi-sdk and the flags of C scripts: its header on its own and its source, each as C99 and
# as GNU C17, wasi-sdk's default, in which the C library's headers define more macros, pedantic and
# with -Wall and -Wextra as errors.
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
    [[  c           write the C bindings of each kind of script, as a C header and source]]
    [[  check       check the interface file, and write nothing]]
    [[  host        write the host's side of the interface, as a C header and source]]
    [[  signatures  print the signature encoding and hash of each host function, method and entry]]
    [[              point]]
    [[  wit         write the interface's WIT rendering]]
    [[options of c and host:]]
    [[  --prefix <prefix>         start every C name with <prefix>, not <namespace>_<name>_]]
    [[options of c, host and wit:]]
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

# Like check, signatures prints only the diagnostics of an interface with errors, and c, host and
# wit print them and write nothing. Every command reports C names and WIT names that clash.
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
stasis_add_generator_output_check(signatures_reports_wit_name_clashes
    DIRECTORY invalid
    ARGS signatures wit_name_clashes.stasis
    EXIT_CODE 1
    DIAGNOSTICS "${CMAKE_CURRENT_SOURCE_DIR}/invalid/wit_name_clashes.stasis"
)
set(nothing_written "${CMAKE_CURRENT_BINARY_DIR}/nothing_written")
stasis_add_generator_output_check(host_writes_nothing_for_an_invalid_interface
    DIRECTORY invalid
    ARGS host --output-directory "${nothing_written}/host" c_name_clashes.stasis
    EXIT_CODE 1
    DIAGNOSTICS "${CMAKE_CURRENT_SOURCE_DIR}/invalid/c_name_clashes.stasis"
    EMPTY_DIRECTORY "${nothing_written}/host"
)
stasis_add_generator_output_check(c_writes_nothing_for_an_invalid_interface
    DIRECTORY invalid
    ARGS c --output-directory "${nothing_written}/c" c_name_clashes.stasis
    EXIT_CODE 1
    DIAGNOSTICS "${CMAKE_CURRENT_SOURCE_DIR}/invalid/c_name_clashes.stasis"
    EMPTY_DIRECTORY "${nothing_written}/c"
)
stasis_add_generator_output_check(wit_writes_nothing_for_an_invalid_interface
    DIRECTORY invalid
    ARGS wit --output-directory "${nothing_written}/wit" wit_name_clashes.stasis
    EXIT_CODE 1
    DIAGNOSTICS "${CMAKE_CURRENT_SOURCE_DIR}/invalid/wit_name_clashes.stasis"
    EMPTY_DIRECTORY "${nothing_written}/wit"
)

# The options of c, host and wit, and the files they write.
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
stasis_add_generator_check(wit_takes_no_prefix ARGS wit --prefix acme_ crlf.stasis EXIT_CODE 2
    LINES "stasis-gen: `wit` takes no `--prefix`" ${usage}
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
stasis_add_generator_check(c_refuses_a_directory_it_cant_write
    ARGS c --output-directory missing crlf.stasis
    EXIT_CODE 2
    LINES "stasis-gen: can't write missing/test_crlf_npc\\.h: .+"
)
stasis_add_generator_check(wit_refuses_a_directory_it_cant_write
    ARGS wit --output-directory missing crlf.stasis
    EXIT_CODE 2
    LINES "stasis-gen: can't write missing/test-crlf\\.wit: .+"
)
foreach(command IN ITEMS c host wit)
    add_test(NAME generator.${command}_rewrites_only_changed_files
        COMMAND "${CMAKE_COMMAND}"
            "-DGENERATOR=${generator}"
            "-DCOMMAND=${command}"
            "-DINTERFACE=${CMAKE_CURRENT_SOURCE_DIR}/valid/crlf.stasis"
            "-DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/rewrites/${command}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_rewrites.cmake"
    )
endforeach()

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

# Adds generator.<command>.<name>, which checks that `command`, c, host or wit, writes exactly the
# files of the golden directory <command>/<name>/ for `interface`, with the options that follow it,
# and adds the commands that rewrite that golden directory to rewrite-generator-goldens.
set(rewrite_commands "")
function(stasis_add_golden command name interface)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${command}/${name}")
    set(golden "${CMAKE_CURRENT_SOURCE_DIR}/${command}/${name}")
    add_test(NAME generator.${command}.${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DCOMMAND=${generator};${command};${ARGN};--output-directory;${output};${interface}"
            "-DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/valid"
            "-DOUTPUT_DIRECTORY=${output}"
            "-DEXPECTED=${golden}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_files.cmake"
    )
    list(APPEND rewrite_commands
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${golden}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${golden}"
        COMMAND "${generator}" ${command} ${ARGN} --output-directory "${golden}" "${interface}"
    )
    set(rewrite_commands "${rewrite_commands}" PARENT_SCOPE)
endfunction()

foreach(interface IN LISTS valid_interfaces)
    cmake_path(GET interface FILENAME file)
    cmake_path(GET interface STEM name)
    stasis_add_golden(host ${name} ${file})
    stasis_add_golden(wit ${name} ${file})
    stasis_add_golden(c ${name} ${file})
endforeach()
stasis_add_golden(host crlf_prefixed crlf.stasis --prefix my_crlf_)
stasis_add_golden(c crlf_prefixed crlf.stasis --prefix my_crlf_)

add_custom_target(rewrite-generator-goldens
    ${rewrite_commands}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/valid"
    COMMENT "Rewriting the generator's golden files from what stasis-gen writes now"
    VERBATIM
)
add_dependencies(rewrite-generator-goldens stasis_generator)

file(GLOB golden_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/host/*/*.h")
foreach(header IN LISTS golden_headers)
    stasis_add_header_checks("${header}")
endforeach()

# wasm-tools reads each golden WIT rendering as a WIT package, and encodes and validates it.
file(GLOB golden_wit CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/wit/*/*.wit")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wit_packages")
foreach(file IN LISTS golden_wit)
    cmake_path(GET file PARENT_PATH directory)
    cmake_path(GET directory FILENAME name)
    add_test(NAME generator.wasm_tools_reads_wit.${name}
        COMMAND "${STASIS_WASM_TOOLS}" component wit --wasm
            -o "${CMAKE_CURRENT_BINARY_DIR}/wit_packages/${name}.wasm" "${file}"
    )
endforeach()

file(GLOB golden_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/host/*/*.c")
foreach(source IN LISTS golden_sources)
    cmake_path(GET source STEM name)
    stasis_add_compile_check(stasis_host_source_check_${name} "${source}" C 99)
endforeach()

# Compiles `source`, which is a golden C bindings' source or includes the golden header `header`,
# with the pinned wasi-sdk's clang as C scripts are compiled, as the C `standard`, pedantic and with
# -Wall and -Wextra as errors, as part of the target stasis_c_bindings_checks.
set(c_bindings_checks "")
function(stasis_add_c_bindings_check name source header standard)
    cmake_path(GET header PARENT_PATH directory)
    set(object "${CMAKE_CURRENT_BINARY_DIR}/c_bindings_checks/${name}_${standard}.o")
    add_custom_command(OUTPUT "${object}"
        COMMAND "${STASIS_WASI_CLANG}" ${STASIS_C_SCRIPT_FLAGS} "--sysroot=${STASIS_WASI_SYSROOT}"
            -std=${standard} -pedantic -Wall -Wextra -Werror "-I${directory}" -c "${source}"
            -o "${object}"
        DEPENDS "${source}" "${header}"
        COMMENT "Compiling ${name} as ${standard}"
        VERBATIM
    )
    list(APPEND c_bindings_checks "${object}")
    set(c_bindings_checks "${c_bindings_checks}" PARENT_SCOPE)
endfunction()

file(GLOB golden_bindings CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/c/*/*.h")
foreach(header IN LISTS golden_bindings)
    cmake_path(GET header PARENT_PATH directory)
    cmake_path(GET directory FILENAME golden)
    cmake_path(GET header STEM name)
    cmake_path(GET header FILENAME file)
    set(header_check "${CMAKE_CURRENT_BINARY_DIR}/c_bindings_checks/${golden}_${name}_header.c")
    file(CONFIGURE OUTPUT "${header_check}" CONTENT "#include <@file@>\n#include <@file@>\n" @ONLY)
    foreach(standard IN ITEMS c99 gnu17)
        stasis_add_c_bindings_check(${golden}_${name}_header "${header_check}" "${header}"
            ${standard}
        )
        stasis_add_c_bindings_check(${golden}_${name} "${directory}/${name}.c" "${header}"
            ${standard}
        )
    endforeach()
endforeach()
add_custom_target(stasis_c_bindings_checks ALL DEPENDS ${c_bindings_checks})

# A golden C bindings' header refuses to compile without __wasm32__ or __wasi__, or with
# __wasm_atomics__ or _REENTRANT, saying why in its first error. The compiler stops there, before
# the C library's headers, which wasi-sdk lacks for other targets.
set(refused_header "${CMAKE_CURRENT_SOURCE_DIR}/c/crlf/test_crlf_npc.h")
set(wrong_target "test_crlf_npc\\.h is for scripts built for wasm32-wasip1")
set(threads "test_crlf_npc\\.h is for scripts built without threads")
foreach(case IN ITEMS
    "wasm64|--target=wasm64-wasip1|${wrong_target}"
    "another_system|--target=wasm32-unknown-unknown|${wrong_target}"
    "atomics|--target=wasm32-wasip1;-matomics|${threads}"
    "reentrant|--target=wasm32-wasip1;-D_REENTRANT|${threads}"
)
    string(REPLACE "|" ";" case "${case}")
    list(POP_FRONT case what)
    list(POP_BACK case message)
    set(command "${STASIS_WASI_CLANG}" ${case} "--sysroot=${STASIS_WASI_SYSROOT}" -fsyntax-only
        -fno-caret-diagnostics -Wfatal-errors -x c "${refused_header}"
    )
    add_test(NAME generator.c_header_refuses.${what}
        COMMAND "${CMAKE_COMMAND}"
            "-DCOMMAND=${command}"
            -DEXIT_CODE=1
            "-DLINES=.*test_crlf_npc\\.h:[0-9]+:[0-9]+: fatal error: \"${message}\""
            -P "${expect_lines}"
    )
endforeach()

set(escaping "${CMAKE_CURRENT_SOURCE_DIR}/host/escaping")
stasis_add_compile_check(stasis_host_macro_check_c escaping_macros.c C 99 "${escaping}")
stasis_add_compile_check(stasis_host_macro_check_cxx escaping_macros.cpp CXX 11 "${escaping}")

# This build generates the host side, the C bindings and the WIT rendering of the sample dungeon
# interface with stasis_generate, compiles the host side, and checks all three against their golden
# files.
stasis_generate(host valid/dungeon.stasis OUTPUT_DIRECTORY generated VARIABLE dungeon_host)
stasis_generate(c valid/dungeon.stasis OUTPUT_DIRECTORY generated VARIABLE dungeon_c)
stasis_generate(wit valid/dungeon.stasis OUTPUT_DIRECTORY generated VARIABLE dungeon_wit)
list(GET dungeon_host 1 dungeon_source)
stasis_add_compile_check(stasis_generated_host_side "${dungeon_source}" C 99
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
add_custom_target(stasis_generated_files ALL DEPENDS ${dungeon_c} ${dungeon_wit})
foreach(command IN ITEMS host c wit)
    foreach(file IN LISTS dungeon_${command})
        cmake_path(GET file FILENAME name)
        add_test(NAME generator.stasis_generate.writes_${name}
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${file}" "${CMAKE_CURRENT_SOURCE_DIR}/${command}/dungeon/${name}"
        )
    endforeach()
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
