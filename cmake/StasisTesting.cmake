# Test programs built on Stasis's own harness, with each case a CTest test of its own, and the
# checks that compile a header on its own as a host's compiler would.

include_guard(GLOBAL)

set(STASIS_REGISTER_TEST_CASES "${CMAKE_CURRENT_LIST_DIR}/register_test_cases.cmake")

# Adds the test program stasis_test_<name>, built from the sources that follow the name and linked
# with the harness, and registers each case it lists as the CTest test <name>.<case>.
#
# The program lists its cases with --list once it's built, so a test file names each case once, in
# its TEST_LIST. Until the program is built, CTest runs a test in its place that fails, saying so.
function(stasis_add_test name)
    set(target stasis_test_${name})
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE stasis_harness)
    stasis_target_conventions(${target})

    set(cases "${CMAKE_CURRENT_BINARY_DIR}/${target}_cases.cmake")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DPROGRAM=$<TARGET_FILE:${target}>"
            "-DPREFIX=${name}."
            "-DOUTPUT=${cases}"
            -P "${STASIS_REGISTER_TEST_CASES}"
        COMMENT "Listing the cases of ${target}"
        VERBATIM
    )
    set(include "${CMAKE_CURRENT_BINARY_DIR}/${target}_include.cmake")
    file(WRITE "${include}"
        "if(EXISTS [==[${cases}]==])\n"
        "    include([==[${cases}]==])\n"
        "else()\n"
        "    add_test([==[${name}.NOT_BUILT]==] [==[${target}_is_not_built]==])\n"
        "endif()\n"
    )
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${include}")
endfunction()

# The newest C++ standard that CMake knows the compiler has, for the header checks.
foreach(standard IN ITEMS 26 23 20 17 14 11)
    if("cxx_std_${standard}" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
        set(STASIS_NEWEST_CXX_STANDARD ${standard})
        break()
    endif()
endforeach()

# Compiles `source` as the object library `target`, in `language` at `standard`, as a host's
# compiler might see it: pedantic, with Stasis's own warnings but not clang-tidy, which is for
# Stasis's own code, and with Stasis's public headers and the directories that follow on the include
# path.
function(stasis_add_compile_check target source language standard)
    add_library(${target} OBJECT "${source}")
    target_include_directories(${target} PRIVATE ${ARGN})
    target_link_libraries(${target} PRIVATE stasis::stasis)
    set_target_properties(${target} PROPERTIES
        ${language}_STANDARD ${standard}
        ${language}_STANDARD_REQUIRED ON
        ${language}_EXTENSIONS OFF
        # No C++ modules, which a check doesn't use, to scan for.
        CXX_SCAN_FOR_MODULES OFF
        C_CLANG_TIDY ""
    )
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -pedantic-errors)
    endif()
    stasis_target_warnings(${target})
endfunction()

# Compiles a header on its own, as stasis_add_compile_check does, with its own directory on the
# include path. `label` names the check. The header is included twice, which its include guard must
# allow.
function(stasis_add_header_check header language standard label)
    cmake_path(GET header FILENAME name)
    cmake_path(GET header PARENT_PATH directory)
    string(MAKE_C_IDENTIFIER "${name}" id)
    if(language STREQUAL "C")
        set(extension c)
    else()
        set(extension cpp)
    endif()
    set(source "${CMAKE_CURRENT_BINARY_DIR}/header_checks/${id}_${label}.${extension}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "#include <@name@>\n#include <@name@>\n" @ONLY)
    stasis_add_compile_check(stasis_header_check_${id}_${label} "${source}" ${language} ${standard}
        "${directory}"
    )
endfunction()

# Compiles a header on its own as C99, as C++11 and as the newest C++, as stasis_add_header_check
# does.
function(stasis_add_header_checks header)
    stasis_add_header_check("${header}" C 99 c99)
    stasis_add_header_check("${header}" CXX 11 cxx11)
    stasis_add_header_check("${header}" CXX ${STASIS_NEWEST_CXX_STANDARD} cxx_newest)
endfunction()
