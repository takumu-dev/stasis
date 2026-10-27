# Runs a command that writes files into a directory, and checks that it succeeds, prints nothing,
# and writes exactly the files of another directory, byte for byte.
#
#   cmake -DCOMMAND=<program;argument;...> -DWORKING_DIRECTORY=<directory>
#         -DOUTPUT_DIRECTORY=<directory> -DEXPECTED=<directory> -P expect_files.cmake
#
# OUTPUT_DIRECTORY is emptied first, and COMMAND writes into it.

cmake_minimum_required(VERSION 3.25...4.4)

file(REMOVE_RECURSE "${OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
execute_process(
    COMMAND ${COMMAND}
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    RESULT_VARIABLE result
)

set(problems "")
if(NOT result EQUAL 0)
    string(APPEND problems "\nIt exited with ${result}, not 0.")
endif()
if(NOT output STREQUAL "")
    string(APPEND problems "\nIt printed:\n${output}")
endif()

file(GLOB written RELATIVE "${OUTPUT_DIRECTORY}" "${OUTPUT_DIRECTORY}/*")
file(GLOB expected RELATIVE "${EXPECTED}" "${EXPECTED}/*")
list(SORT written)
list(SORT expected)
if(NOT written STREQUAL expected)
    string(APPEND problems "\nIt wrote [${written}], where it should have written [${expected}].")
endif()

# A file that differs is reported at its first line that differs. The lines are taken one at a
# time, since a line may hold characters that a CMake list would split on.
foreach(name IN LISTS expected)
    if(NOT name IN_LIST written)
        continue()
    endif()
    file(READ "${OUTPUT_DIRECTORY}/${name}" actual_text)
    file(READ "${EXPECTED}/${name}" expected_text)
    if(actual_text STREQUAL expected_text)
        continue()
    endif()
    set(number 1)
    while(TRUE)
        string(FIND "${actual_text}" "\n" actual_end)
        string(FIND "${expected_text}" "\n" expected_end)
        string(SUBSTRING "${actual_text}" 0 ${actual_end} actual_line)
        string(SUBSTRING "${expected_text}" 0 ${expected_end} expected_line)
        if(NOT actual_line STREQUAL expected_line OR actual_end EQUAL -1 OR expected_end EQUAL -1)
            break()
        endif()
        math(EXPR actual_end "${actual_end} + 1")
        math(EXPR expected_end "${expected_end} + 1")
        string(SUBSTRING "${actual_text}" ${actual_end} -1 actual_text)
        string(SUBSTRING "${expected_text}" ${expected_end} -1 expected_text)
        math(EXPR number "${number} + 1")
    endwhile()
    string(APPEND problems
        "\n${name} differs first at line ${number}, which is:\n${actual_line}\n"
        "where it should be:\n${expected_line}"
    )
endforeach()

if(problems)
    list(JOIN COMMAND " " command)
    message(FATAL_ERROR "${command}${problems}")
endif()
