# Runs a command and checks how it ended: its exit status, and its output, stdout and stderr
# together.
#
#   cmake -DCOMMAND=<program;argument;...> -DEXIT_CODE=<status> -DLINES=<regex;...> -P expect.cmake
#
# LINES gives every line of the output, each as a regular expression that must match the whole
# line.

cmake_minimum_required(VERSION 3.25...4.4)

execute_process(
    COMMAND ${COMMAND}
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    RESULT_VARIABLE result
)
# On Windows, a program's lines end in CRLF.
string(REPLACE "\r" "" output "${output}")

set(problems "")
if(NOT result STREQUAL EXIT_CODE)
    string(APPEND problems "\n  It exited with ${result}, not ${EXIT_CODE}.")
endif()

# The output's lines are taken one at a time from the text, since a line may hold characters that
# a CMake list would split on.
set(rest "${output}")
set(number 0)
foreach(regex IN LISTS LINES)
    math(EXPR number "${number} + 1")
    string(FIND "${rest}" "\n" end)
    if(end EQUAL -1)
        string(APPEND problems "\n  It printed no line ${number}, to match: ${regex}")
        break()
    endif()
    string(SUBSTRING "${rest}" 0 ${end} line)
    math(EXPR end "${end} + 1")
    string(SUBSTRING "${rest}" ${end} -1 rest)
    if(NOT line MATCHES "^${regex}$")
        string(APPEND problems "\n  Its line ${number} doesn't match: ${regex}")
    endif()
endforeach()
if(NOT end EQUAL -1 AND NOT rest STREQUAL "")
    string(APPEND problems "\n  It printed more than ${number} lines.")
endif()

if(problems)
    list(JOIN COMMAND " " command)
    message(FATAL_ERROR "${command}${problems}\nIts output:\n${output}")
endif()
