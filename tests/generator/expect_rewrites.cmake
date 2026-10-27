# Checks that a command of stasis-gen that writes files, host or wit, leaves alone a file that holds
# what it would write, and rewrites one that doesn't.
#
#   cmake -DGENERATOR=<stasis-gen> -DCOMMAND=<host|wit> -DINTERFACE=<file>
#         -DOUTPUT_DIRECTORY=<directory> -P expect_rewrites.cmake
#
# It writes the files for INTERFACE into an empty OUTPUT_DIRECTORY, and writes them again over a
# second later, which must leave each file as it was, modification time included. It then changes
# the first file and writes them once more, which must give that file what was first written again.

cmake_minimum_required(VERSION 3.25...4.4)

# Runs the command, which must succeed and print nothing.
function(stasis_write_files)
    execute_process(
        COMMAND "${GENERATOR}" ${COMMAND} --output-directory "${OUTPUT_DIRECTORY}" "${INTERFACE}"
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0 OR NOT output STREQUAL "")
        message(FATAL_ERROR "stasis-gen ${COMMAND} exited with ${result}, printing:\n${output}")
    endif()
endfunction()

file(REMOVE_RECURSE "${OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
stasis_write_files()
file(GLOB files "${OUTPUT_DIRECTORY}/*")
list(SORT files)
if(NOT files)
    message(FATAL_ERROR "stasis-gen ${COMMAND} wrote nothing.")
endif()
set(index 0)
foreach(file IN LISTS files)
    file(READ "${file}" text_${index})
    file(TIMESTAMP "${file}" time_${index} "%s")
    math(EXPR index "${index} + 1")
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.5)
stasis_write_files()
set(problems "")
set(index 0)
foreach(file IN LISTS files)
    file(READ "${file}" text)
    file(TIMESTAMP "${file}" time "%s")
    if(NOT text STREQUAL text_${index} OR NOT time STREQUAL time_${index})
        string(APPEND problems "\n${file} was rewritten, though it held what was written already.")
    endif()
    math(EXPR index "${index} + 1")
endforeach()

list(GET files 0 changed)
file(APPEND "${changed}" "/* changed */\n")
stasis_write_files()
file(READ "${changed}" text)
if(NOT text STREQUAL text_0)
    string(APPEND problems "\n${changed} wasn't rewritten, though it had changed.")
endif()
if(problems)
    message(FATAL_ERROR "stasis-gen ${COMMAND}:${problems}")
endif()
