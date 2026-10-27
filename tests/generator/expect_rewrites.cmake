# Checks that `stasis-gen host` leaves alone a file that holds what it would write, and rewrites one
# that doesn't.
#
#   cmake -DGENERATOR=<stasis-gen> -DINTERFACE=<file> -DOUTPUT_DIRECTORY=<directory>
#         -P expect_rewrites.cmake
#
# It writes the host side of INTERFACE into an empty OUTPUT_DIRECTORY, changes the header, and
# writes the host side again over a second later. The header must then hold what was first written
# again, and the source must keep its modification time.

cmake_minimum_required(VERSION 3.25...4.4)

# Runs stasis-gen host, which must succeed and print nothing.
function(stasis_write_host_side)
    execute_process(
        COMMAND "${GENERATOR}" host --output-directory "${OUTPUT_DIRECTORY}" "${INTERFACE}"
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0 OR NOT output STREQUAL "")
        message(FATAL_ERROR "stasis-gen host exited with ${result}, printing:\n${output}")
    endif()
endfunction()

file(REMOVE_RECURSE "${OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
stasis_write_host_side()
file(GLOB header "${OUTPUT_DIRECTORY}/*.h")
file(GLOB source "${OUTPUT_DIRECTORY}/*.c")
file(READ "${header}" header_text)
file(READ "${source}" source_text)
file(TIMESTAMP "${source}" source_time "%s")

file(APPEND "${header}" "/* changed */\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.5)
stasis_write_host_side()

set(problems "")
file(READ "${header}" header_again)
if(NOT header_again STREQUAL header_text)
    string(APPEND problems "\nThe changed header wasn't rewritten.")
endif()
file(READ "${source}" source_again)
file(TIMESTAMP "${source}" source_time_again "%s")
if(NOT source_again STREQUAL source_text OR NOT source_time_again STREQUAL source_time)
    string(APPEND problems "\nThe unchanged source was rewritten.")
endif()
if(problems)
    message(FATAL_ERROR "${problems}")
endif()
