# stasis_generate, which runs stasis-gen as part of a build, to write an interface's host side, its
# C bindings or its WIT rendering.
#
#   stasis_generate(<host|c|wit> <interface file> OUTPUT_DIRECTORY <directory> VARIABLE <variable>
#                   [PREFIX <prefix>])
#
# It adds a custom command that writes the files into OUTPUT_DIRECTORY, and that runs again whenever
# the interface file or stasis-gen changes. It sets VARIABLE to the files' paths, for
# target_sources. A relative interface file is found from the current source directory, and a
# relative OUTPUT_DIRECTORY from the current binary directory. PREFIX starts every C name in place
# of `<namespace>_<name>_`, for host and c.
#
#   stasis_generate(host dungeon.stasis OUTPUT_DIRECTORY gen VARIABLE dungeon_host)
#   target_sources(game PRIVATE ${dungeon_host})
#
# The files are named after the prefix, or for wit, after the interface's namespace and name, so
# stasis_generate reads the interface file's header as the build is configured, and the build is
# configured again whenever the interface file changes. host writes <prefix>.h and <prefix>.c, with
# the prefix's last `_` dropped, c writes <prefix><kind of script>.h and .c for each kind of script,
# and wit writes <namespace>-<name>.wit.
#
# stasis-gen leaves alone a file that holds what it would write already. Ninja then rebuilds nothing
# that depends on it, but other generators may run stasis-gen again in each build, until a change
# to the interface changes what it writes.
#
# stasis-gen is the target stasis::generator, which any build that isn't cross-compiling defines. A
# cross-compiling build names in STASIS_GENERATOR the stasis-gen of a native build of the same
# version of Stasis, which stasis_generate checks with --version, and without which configuring
# stops at the first stasis_generate. STASIS_GENERATOR takes the place of stasis::generator in any
# other build too.

include_guard(GLOBAL)

set(STASIS_GENERATOR "" CACHE FILEPATH
    "The stasis-gen that stasis_generate runs in place of stasis::generator, as a cross-compiling \
build needs: that of a native build of the same version of Stasis."
)

# The version of Stasis whose stasis-gen stasis_generate runs.
set_property(GLOBAL PROPERTY STASIS_GENERATE_VERSION "${stasis_VERSION}")

# Sets `generator` to the stasis-gen that stasis_generate runs, and `depends` to what its commands
# depend on for it.
function(stasis_generate_find_generator generator depends)
    get_property(version GLOBAL PROPERTY STASIS_GENERATE_VERSION)
    if(STASIS_GENERATOR)
        get_property(checked GLOBAL PROPERTY STASIS_GENERATE_CHECKED)
        if(NOT checked STREQUAL STASIS_GENERATOR)
            execute_process(COMMAND "${STASIS_GENERATOR}" --version
                OUTPUT_VARIABLE output
                ERROR_QUIET
                RESULT_VARIABLE result
            )
            string(STRIP "${output}" output)
            if(NOT result EQUAL 0 OR NOT output STREQUAL "stasis-gen ${version}")
                message(FATAL_ERROR
                    "STASIS_GENERATOR is ${STASIS_GENERATOR}, which reports \"${output}\" rather "
                    "than \"stasis-gen ${version}\". It must be the stasis-gen of a native build "
                    "of Stasis ${version}."
                )
            endif()
            set_property(GLOBAL PROPERTY STASIS_GENERATE_CHECKED "${STASIS_GENERATOR}")
        endif()
        set(${generator} "${STASIS_GENERATOR}" PARENT_SCOPE)
        set(${depends} "${STASIS_GENERATOR}" PARENT_SCOPE)
    elseif(TARGET stasis::generator)
        # A target of this build is depended on by name, so that the build makes it first.
        get_target_property(target stasis::generator ALIASED_TARGET)
        if(NOT target)
            set(target stasis::generator)
        endif()
        get_target_property(imported ${target} IMPORTED)
        set(${generator} "$<TARGET_FILE:${target}>" PARENT_SCOPE)
        if(imported)
            set(${depends} "$<TARGET_FILE:${target}>" PARENT_SCOPE)
        else()
            set(${depends} ${target} PARENT_SCOPE)
        endif()
    else()
        message(FATAL_ERROR
            "stasis_generate runs stasis-gen, which a cross-compiling build can't build. Set "
            "STASIS_GENERATOR to the stasis-gen of a native build of Stasis ${version}."
        )
    endif()
endfunction()

# Adds the command that runs stasis-gen, as the comment at the top of this file says.
function(stasis_generate kind file)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "OUTPUT_DIRECTORY;VARIABLE;PREFIX" "")
    if(NOT kind MATCHES "^(host|c|wit)$")
        message(FATAL_ERROR "stasis_generate writes host, c or wit, not ${kind}.")
    endif()
    if(arg_UNPARSED_ARGUMENTS OR NOT arg_OUTPUT_DIRECTORY OR NOT arg_VARIABLE)
        message(FATAL_ERROR
            "stasis_generate takes <host|c|wit> <interface file> OUTPUT_DIRECTORY <directory> "
            "VARIABLE <variable> [PREFIX <prefix>]."
        )
    endif()
    if(DEFINED arg_PREFIX AND kind STREQUAL "wit")
        message(FATAL_ERROR "stasis_generate takes a PREFIX only for host and c.")
    endif()
    cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    set(directory "${arg_OUTPUT_DIRECTORY}")
    cmake_path(ABSOLUTE_PATH directory BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
    stasis_generate_find_generator(generator depends)

    # The names in the interface's header and its kinds of script, from its text without its
    # comments, which is all the files' names need. stasis-gen checks the rest as it runs.
    set(options "")
    if(DEFINED arg_PREFIX)
        set(prefix "${arg_PREFIX}")
        list(APPEND options --prefix "${arg_PREFIX}")
    endif()
    if(NOT DEFINED arg_PREFIX OR kind STREQUAL "c")
        file(READ "${file}" text)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
        string(REGEX REPLACE "//[^\n]*" "" text "${text}")
        set(blank "[ \t\r\n]")
        set(word "[a-z][a-z0-9_]*")
        if(NOT text MATCHES
            "(^|[^a-z0-9_])interface${blank}+(${word})${blank}*:${blank}*(${word})${blank}*@"
        )
            message(FATAL_ERROR
                "stasis_generate can't find the header, `interface <namespace>:<name>@<version>;`, "
                "of ${file}. `stasis-gen check` reports what's wrong with it."
            )
        endif()
        set(space "${CMAKE_MATCH_2}")
        set(name "${CMAKE_MATCH_3}")
        if(NOT DEFINED prefix)
            set(prefix "${space}_${name}_")
        endif()
        string(REGEX MATCHALL "(^|[^a-z0-9_])script${blank}+${word}" scripts "${text}")
        list(TRANSFORM scripts REPLACE "^.*script${blank}+" "")
    endif()

    if(kind STREQUAL "host")
        string(REGEX REPLACE "_$" "" base "${prefix}")
        set(outputs "${directory}/${base}.h" "${directory}/${base}.c")
    elseif(kind STREQUAL "c")
        set(outputs "")
        foreach(script IN LISTS scripts)
            list(APPEND outputs "${directory}/${prefix}${script}.h" "${directory}/${prefix}${script}.c")
        endforeach()
    else()
        string(REPLACE "_" "-" wit "${space}-${name}")
        set(outputs "${directory}/${wit}.wit")
    endif()

    cmake_path(GET file FILENAME file_name)
    file(MAKE_DIRECTORY "${directory}")
    add_custom_command(OUTPUT ${outputs}
        COMMAND "${generator}" ${kind} ${options} --output-directory "${directory}" "${file}"
        DEPENDS "${file}" ${depends}
        COMMENT "Generating the ${kind} files of ${file_name} with stasis-gen"
        VERBATIM
    )
    set(${arg_VARIABLE} ${outputs} PARENT_SCOPE)
endfunction()
