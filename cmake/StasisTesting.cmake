# Test programs built on Stasis's own harness, with each case a CTest test of its own.

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
