# Stasis's tests, each case a CTest test of its own:
#
#   harness.*  the harness's own checks
#   headers.*  what the public headers include
#   scripts.*  scripts built from source, as the tests build them
#   sha256.*   SHA-256 against NIST's test vectors
#   status.*   the status codes, through the C API
#
# Every program is built on the harness in harness/. The scripts the tests run are built from source
# with the pinned wasm-tools and wasi-sdk, so configuring the tests needs both. The build itself
# checks that each public header compiles on its own as C99 and as C++, with the presets' warnings
# as errors. Only those checks are C++: the library never is.

enable_language(CXX)

include(StasisTesting)
include(StasisScripts)

add_subdirectory(harness)
add_subdirectory(scripts)

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

# Compiles a public header on its own, as a host's compiler might see it, in `language` at
# `standard`, and pedantic. `label` names the check.
function(stasis_add_header_check header language standard label)
    cmake_path(GET header FILENAME name)
    string(MAKE_C_IDENTIFIER "${name}" id)
    if(language STREQUAL "C")
        set(extension c)
    else()
        set(extension cpp)
    endif()
    set(target stasis_header_check_${id}_${label})
    set(source "${CMAKE_CURRENT_BINARY_DIR}/header_checks/${id}_${label}.${extension}")
    # The header is included twice, which its include guard must allow.
    file(CONFIGURE OUTPUT "${source}" CONTENT "#include <@name@>\n#include <@name@>\n" @ONLY)
    add_library(${target} OBJECT "${source}")
    target_link_libraries(${target} PRIVATE stasis::stasis)
    set_target_properties(${target} PROPERTIES
        ${language}_STANDARD ${standard}
        ${language}_STANDARD_REQUIRED ON
        ${language}_EXTENSIONS OFF
        # No C++ modules, which a header check doesn't use, to scan for.
        CXX_SCAN_FOR_MODULES OFF
        # A check includes the header and uses none of it, which clang-tidy would report.
        C_CLANG_TIDY ""
    )
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -pedantic-errors)
    endif()
    stasis_target_warnings(${target})
endfunction()

# The newest C++ standard that CMake knows the compiler has.
foreach(standard IN ITEMS 26 23 20 17 14 11)
    if("cxx_std_${standard}" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
        set(newest_cxx ${standard})
        break()
    endif()
endforeach()

get_target_property(public_headers stasis HEADER_SET)
foreach(header IN LISTS public_headers)
    stasis_add_header_check("${header}" C 99 c99)
    stasis_add_header_check("${header}" CXX 11 cxx11)
    stasis_add_header_check("${header}" CXX ${newest_cxx} cxx_newest)
endforeach()
add_test(NAME headers.include_only_standard_headers
    COMMAND "${CMAKE_COMMAND}"
        "-DHEADERS=${public_headers}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_header_includes.cmake"
)
