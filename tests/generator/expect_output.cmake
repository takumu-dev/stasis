# Runs a command and checks exactly how it ended: its exit status, everything it wrote to stdout,
# and everything it wrote to stderr.
#
#   cmake -DCOMMAND=<program;argument;...> -DWORKING_DIRECTORY=<directory> -DEXIT_CODE=<status>
#         [-DSTDOUT=<file>] [-DDIAGNOSTICS=<file>] [-DEMPTY_DIRECTORY=<directory>]
#         -P expect_output.cmake
#
# stdout must be exactly STDOUT's contents, and stderr exactly the lines of DIAGNOSTICS that start
# with "//! ", without that prefix. Either one left out means the command writes nothing there.
# EMPTY_DIRECTORY, if given, is emptied first, and the command must leave it empty.

cmake_minimum_required(VERSION 3.25...4.4)

if(DEFINED EMPTY_DIRECTORY)
    file(REMOVE_RECURSE "${EMPTY_DIRECTORY}")
    file(MAKE_DIRECTORY "${EMPTY_DIRECTORY}")
endif()

execute_process(
    COMMAND ${COMMAND}
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    RESULT_VARIABLE result
)
# On Windows, a program's lines end in CRLF.
string(REPLACE "\r" "" stdout "${stdout}")
string(REPLACE "\r" "" stderr "${stderr}")

set(expected_stdout "")
if(DEFINED STDOUT)
    file(READ "${STDOUT}" expected_stdout)
    string(REPLACE "\r" "" expected_stdout "${expected_stdout}")
endif()

# The lines are taken one at a time from the text, since a line may hold characters that a CMake
# list would split on.
set(expected_stderr "")
if(DEFINED DIAGNOSTICS)
    file(READ "${DIAGNOSTICS}" rest)
    string(REPLACE "\r" "" rest "${rest}")
    while(NOT rest STREQUAL "")
        string(FIND "${rest}" "\n" end)
        if(end EQUAL -1)
            set(line "${rest}")
            set(rest "")
        else()
            string(SUBSTRING "${rest}" 0 ${end} line)
            math(EXPR end "${end} + 1")
            string(SUBSTRING "${rest}" ${end} -1 rest)
        endif()
        if(line MATCHES "^//! ")
            string(SUBSTRING "${line}" 4 -1 line)
            string(APPEND expected_stderr "${line}\n")
        endif()
    endwhile()
endif()

set(problems "")
if(NOT result STREQUAL EXIT_CODE)
    string(APPEND problems "\nIt exited with ${result}, not ${EXIT_CODE}.")
endif()
if(NOT "${stdout}" STREQUAL "${expected_stdout}")
    string(APPEND problems
        "\nIts stdout was:\n${stdout}\nwhere it should have been:\n${expected_stdout}"
    )
endif()
if(NOT "${stderr}" STREQUAL "${expected_stderr}")
    string(APPEND problems
        "\nIts stderr was:\n${stderr}\nwhere it should have been:\n${expected_stderr}"
    )
endif()

if(DEFINED EMPTY_DIRECTORY)
    file(GLOB written RELATIVE "${EMPTY_DIRECTORY}" "${EMPTY_DIRECTORY}/*")
    if(written)
        string(APPEND problems "\nIt wrote ${written}, where it should have written nothing.")
    endif()
endif()

if(problems)
    list(JOIN COMMAND " " command)
    message(FATAL_ERROR "${command}${problems}")
endif()
