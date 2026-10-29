# Stasis's tests, each case a CTest test of its own:
#
#   generator.*       stasis-gen, through its command line
#   handle_limit.*    instances running out of handles, through the C API of the test-only library
#   handles.*         handles crossing between hosts and scripts, through the C API
#   harness.*         the harness's own checks
#   headers.*         what the public headers include
#   host_functions.*  scripts calling host functions, through the C API
#   instances.*       creating, starting, calling and destroying script instances, through the C API
#   libraries.*       creating and destroying libraries, through the C API
#   library.*         what the built library holds, exports and references
#   licenses.*        the licence check, on dependencies of its own
#   load.*            loading scripts and the load check, through the C API
#   printing.*        where WAMR's printing goes when a script traps, in the library and the
#                     test-only one
#   scalars.*         scalars crossing to entry points and back, through the C API
#   scripts.*         scripts built from source, as the tests build them
#   sha256.*          SHA-256 against NIST's test vectors
#   status.*          the status codes, through the C API
#   traps.*           calls that trap, and the trap reports they leave, through the C API
#
# Every program is built on the harness in harness/. The scripts the tests run are built from source
# with the pinned wasm-tools and wasi-sdk, so configuring the tests needs both. The build itself
# checks that each public header compiles on its own as C99 and as C++, with the presets' warnings
# as errors. Only those checks are C++: the library never is.

enable_language(CXX)

include(StasisTesting)
include(StasisScripts)

if(TARGET stasis_generator)
    add_subdirectory(generator)
endif()
add_subdirectory(harness)
add_subdirectory(licenses)
add_subdirectory(scripts)
add_subdirectory(api)

# The test-only library: the core and WAMR compiled again, as a static library of their own, for the
# few tests that need what the library proper hides. In it, WAMR's printing goes to the test's
# output, and STASIS_TESTING has the core count what it and WAMR allocate.
stasis_add_wamr(stasis_wamr_testing VPRINTF stasis_testing_vprintf)
add_library(stasis_testing STATIC
    ${STASIS_CORE_SOURCES}
    wamr_output.c
    $<TARGET_OBJECTS:stasis_sha256>
    $<TARGET_OBJECTS:stasis_signature>
)
target_include_directories(stasis_testing PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(stasis_testing PRIVATE STASIS_TESTING)
stasis_target_holds_wamr(stasis_testing stasis_wamr_testing)
stasis_target_conventions(stasis_testing)

# A test program links only the objects it calls from a static library, so a program that links
# the whole of each shows that all of WAMR links, with the system libraries it calls. A shared
# library is linked whole already.
set(whole_libraries stasis_testing)
get_target_property(library_type stasis TYPE)
if(library_type STREQUAL "STATIC_LIBRARY")
    list(APPEND whole_libraries stasis)
endif()
foreach(library IN LISTS whole_libraries)
    add_executable(stasis_link_check_${library} link_check.c)
    target_link_libraries(stasis_link_check_${library} PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,${library}>"
    )
    stasis_target_conventions(stasis_link_check_${library})
endforeach()

# SHA-256 is the one part of the library tested directly, since no function of the API can hand it
# known messages. NIST's files are read from the source tree.
stasis_add_test(sha256 sha256_test.c)
target_link_libraries(stasis_test_sha256 PRIVATE stasis_sha256)
target_compile_definitions(stasis_test_sha256 PRIVATE
    STASIS_NIST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/nist"
)

stasis_add_test(status status_test.c)
target_link_libraries(stasis_test_status PRIVATE stasis::stasis)

# stasis_status stays 32 bits even where a host's compiler makes each enum as small as its values
# allow, which the build checks by compiling status_size.c that way.
if(NOT MSVC)
    add_library(stasis_status_size OBJECT status_size.c)
    target_link_libraries(stasis_status_size PRIVATE stasis::stasis)
    target_compile_options(stasis_status_size PRIVATE -fshort-enums)
    stasis_target_conventions(stasis_status_size)
endif()

get_target_property(public_headers stasis HEADER_SET)
foreach(header IN LISTS public_headers)
    stasis_add_header_checks("${header}")
endforeach()
add_test(NAME headers.include_only_standard_headers
    COMMAND "${CMAKE_COMMAND}"
        "-DHEADERS=${public_headers}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_header_includes.cmake"
)

# The library's own checks read the symbols it defines, exports and references: with nm, or with
# dumpbin, which sits beside MSVC's linker. Only a shared library exports anything.
if(MSVC)
    cmake_path(GET CMAKE_LINKER PARENT_PATH linker_dir)
    find_program(STASIS_DUMPBIN dumpbin HINTS "${linker_dir}" REQUIRED)
    set(symbol_tool "-DDUMPBIN=${STASIS_DUMPBIN}")
else()
    set(symbol_tool "-DNM=${CMAKE_NM}")
endif()
set(library_checks holds_wamr references_no_signal_or_jump_functions)
if(library_type STREQUAL "SHARED_LIBRARY")
    set(shared TRUE)
    list(APPEND library_checks exports_only_stasis_api)
else()
    set(shared FALSE)
endif()
foreach(check IN LISTS library_checks)
    add_test(NAME library.${check}
        COMMAND "${CMAKE_COMMAND}"
            "-DCHECK=${check}"
            "-DLIBRARY=$<TARGET_FILE:stasis>"
            "-DSHARED=${shared}"
            "${symbol_tool}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_library.cmake"
    )
endforeach()
