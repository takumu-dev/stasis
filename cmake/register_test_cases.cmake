# Runs a test program built on Stasis's harness with --list, and writes a CTest file that adds each
# case it names as a test of its own, called PREFIX followed by the case's name.
#
#   cmake -DPROGRAM=<program> -DPREFIX=<prefix> -DOUTPUT=<file> -P register_test_cases.cmake

cmake_minimum_required(VERSION 3.25...4.4)

execute_process(
    COMMAND "${PROGRAM}" --list
    OUTPUT_VARIABLE cases
    ERROR_VARIABLE error
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${PROGRAM} --list failed (${result}):\n${error}")
endif()

# On Windows, a program's lines end in CRLF.
string(REPLACE "\r" "" cases "${cases}")
string(REGEX MATCHALL "[^\n]+" cases "${cases}")
set(tests "")
foreach(case IN LISTS cases)
    string(APPEND tests "add_test([==[${PREFIX}${case}]==] [==[${PROGRAM}]==] [==[${case}]==])\n")
endforeach()
file(WRITE "${OUTPUT}" "${tests}")
