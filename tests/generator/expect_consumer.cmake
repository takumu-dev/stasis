# Configures and builds the host's build in consumer/, and checks how it ends.
#
#   cmake -DCASE=<case> -DSTASIS_SOURCE_DIR=<directory> -DBUILD_DIRECTORY=<directory>
#         -DGENERATOR=<stasis-gen> -DVERSION=<version> -DINTERFACE=<file> -DEXPECTED=<directory>
#         -DCMAKE_GENERATOR=<generator> -DMAKE_PROGRAM=<program> -P expect_consumer.cmake
#
# CASE is one of:
#
#   uses_stasis_generator     with STASIS_GENERATOR set to GENERATOR, configuring and building
#                             succeed, and generate exactly the files of EXPECTED
#   reruns_when_its_inputs_change
#                             with copies of GENERATOR and INTERFACE, a second build generates
#                             nothing, and a build after either copy is touched generates again
#   refuses_another_version   with STASIS_GENERATOR set to GENERATOR, configuring a build of
#                             another version of Stasis fails, naming the version it needs
#   needs_a_generator         without STASIS_GENERATOR, configuring fails, saying to set it
#
# CMake rewraps a message's lines, so they're compared with every run of blanks as one space.

cmake_minimum_required(VERSION 3.25...4.4)

set(version "${VERSION}")
set(options "")
set(tools "${BUILD_DIRECTORY}-tools")
if(CASE STREQUAL "uses_stasis_generator")
    list(APPEND options "-DSTASIS_GENERATOR=${GENERATOR}")
elseif(CASE STREQUAL "reruns_when_its_inputs_change")
    file(REMOVE_RECURSE "${tools}")
    file(COPY "${GENERATOR}" "${INTERFACE}" DESTINATION "${tools}")
    cmake_path(GET GENERATOR FILENAME generator_name)
    cmake_path(GET INTERFACE FILENAME interface_name)
    set(GENERATOR "${tools}/${generator_name}")
    set(INTERFACE "${tools}/${interface_name}")
    list(APPEND options "-DSTASIS_GENERATOR=${GENERATOR}")
elseif(CASE STREQUAL "refuses_another_version")
    set(version 9.9.9)
    list(APPEND options "-DSTASIS_GENERATOR=${GENERATOR}")
    string(CONCAT expected
        "STASIS_GENERATOR is ${GENERATOR}, which reports \"stasis-gen ${VERSION}\" rather than "
        "\"stasis-gen 9.9.9\". It must be the stasis-gen of a native build of Stasis 9.9.9."
    )
elseif(CASE STREQUAL "needs_a_generator")
    string(CONCAT expected
        "stasis_generate runs stasis-gen, which a cross-compiling build can't build. Set "
        "STASIS_GENERATOR to the stasis-gen of a native build of Stasis ${VERSION}."
    )
else()
    message(FATAL_ERROR "There's no case ${CASE}.")
endif()

file(REMOVE_RECURSE "${BUILD_DIRECTORY}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_LIST_DIR}/consumer"
        -B "${BUILD_DIRECTORY}"
        -G "${CMAKE_GENERATOR}"
        "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}"
        "-DSTASIS_SOURCE_DIR=${STASIS_SOURCE_DIR}"
        "-Dstasis_VERSION=${version}"
        "-DINTERFACE=${INTERFACE}"
        ${options}
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    RESULT_VARIABLE result
)

if(DEFINED expected)
    string(REGEX REPLACE "[ \t\r\n]+" " " flowed "${output}")
    if(result EQUAL 0)
        message(FATAL_ERROR "Configuring succeeded, where it should have failed:\n${output}")
    endif()
    string(FIND "${flowed}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Configuring failed without saying:\n${expected}\nIt said:\n${output}")
    endif()
    return()
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Configuring failed:\n${output}")
endif()

# Builds the host's build, and checks whether it generated anything, as GENERATES says.
function(stasis_build_consumer generates)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIRECTORY}"
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Building failed:\n${output}")
    endif()
    string(FIND "${output}" "Generating the host files of" found)
    if(generates AND found EQUAL -1)
        message(FATAL_ERROR "The build generated nothing, where it should have:\n${output}")
    elseif(NOT generates AND NOT found EQUAL -1)
        message(FATAL_ERROR "The build generated again, where it shouldn't have:\n${output}")
    endif()
endfunction()

stasis_build_consumer(TRUE)
if(CASE STREQUAL "reruns_when_its_inputs_change")
    stasis_build_consumer(FALSE)
    file(TOUCH "${INTERFACE}")
    stasis_build_consumer(TRUE)
    file(TOUCH "${GENERATOR}")
    stasis_build_consumer(TRUE)
endif()
file(GLOB written RELATIVE "${BUILD_DIRECTORY}/generated" "${BUILD_DIRECTORY}/generated/*")
file(GLOB expected_files RELATIVE "${EXPECTED}" "${EXPECTED}/*")
list(SORT written)
list(SORT expected_files)
if(NOT written STREQUAL expected_files)
    message(FATAL_ERROR "The build generated [${written}], not [${expected_files}].")
endif()
foreach(name IN LISTS expected_files)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${BUILD_DIRECTORY}/generated/${name}" "${EXPECTED}/${name}"
        RESULT_VARIABLE different
    )
    if(different)
        message(FATAL_ERROR "The build generated a ${name} that differs from ${EXPECTED}/${name}.")
    endif()
endforeach()
