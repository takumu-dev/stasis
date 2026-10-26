# The pinned wasi-sdk, which tools/install.cmake installs. The tests build C scripts with its clang,
# and the checks format and tidy Stasis's own C with its clang-format and clang-tidy, so that every
# machine formats and checks the code alike.
#
# STASIS_WASI_SDK_PATH says where it is. Until it's set, it takes its value from the WASI_SDK_PATH
# environment variable.

include_guard(GLOBAL)

include(StasisPins)

if(NOT STASIS_WASI_SDK_PATH)
    file(TO_CMAKE_PATH "$ENV{WASI_SDK_PATH}" stasis_wasi_sdk_path)
    set(STASIS_WASI_SDK_PATH "${stasis_wasi_sdk_path}" CACHE PATH
        "The pinned wasi-sdk. It defaults to the WASI_SDK_PATH environment variable."
        FORCE
    )
    unset(stasis_wasi_sdk_path)
endif()

# Sets `variable` to `path` in the pinned wasi-sdk, or to <variable>-NOTFOUND where
# STASIS_WASI_SDK_PATH holds no wasi-sdk, another version of it, or nothing at `path`. With
# REQUIRED_BY, any of those stops the configuration instead, saying what needs the path.
#
#   stasis_wasi_sdk_path(<variable> <path> [REQUIRED_BY <what needs it>])
function(stasis_wasi_sdk_path variable path)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "REQUIRED_BY" "")
    stasis_pinned_version(pinned wasi-sdk)
    set(sdk "${STASIS_WASI_SDK_PATH}")

    # wasi-sdk's VERSION file starts with its version.
    set(version "")
    if(sdk AND EXISTS "${sdk}/VERSION")
        file(STRINGS "${sdk}/VERSION" version LIMIT_COUNT 1)
    endif()
    if(NOT sdk)
        set(problem
            "neither STASIS_WASI_SDK_PATH nor the WASI_SDK_PATH environment variable says where it is"
        )
    elseif(NOT version)
        set(problem "STASIS_WASI_SDK_PATH names ${sdk}, which holds no wasi-sdk")
    elseif(NOT version STREQUAL pinned)
        set(problem "STASIS_WASI_SDK_PATH names ${sdk}, which holds wasi-sdk ${version}")
    elseif(NOT EXISTS "${sdk}/${path}")
        set(problem "${sdk} has no ${path}")
    else()
        set(${variable} "${sdk}/${path}" PARENT_SCOPE)
        return()
    endif()

    if(arg_REQUIRED_BY)
        message(FATAL_ERROR
            "${arg_REQUIRED_BY} needs wasi-sdk ${pinned}, but ${problem}. tools/install.cmake "
            "installs it."
        )
    endif()
    set(${variable} "${variable}-NOTFOUND" PARENT_SCOPE)
endfunction()

# Sets `variable` to wasi-sdk's program `name`, as stasis_wasi_sdk_path does.
#
#   stasis_wasi_sdk_program(<variable> <name> [REQUIRED_BY <what needs it>])
function(stasis_wasi_sdk_program variable name)
    if(CMAKE_HOST_WIN32)
        string(APPEND name .exe)
    endif()
    stasis_wasi_sdk_path(${variable} "bin/${name}" ${ARGN})
    set(${variable} "${${variable}}" PARENT_SCOPE)
endfunction()
