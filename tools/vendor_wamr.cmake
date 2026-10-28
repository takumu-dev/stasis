# Copies WAMR into third_party/wamr/ from a git checkout of its release, unmodified: only the files
# that cmake/StasisWamrFiles.cmake lists, which are those Stasis's build uses, and its licences.
#
#   cmake -DSOURCE=<checkout> -P vendor_wamr.cmake
#
# The checkout must hold the release that third_party/dependencies.json names: core/version.h must
# give its version, and the checkout must be at its commit. Every listed file must be there, and
# the script stops, naming each one missing, before it changes anything. It then replaces
# third_party/wamr/ as a whole, so that nothing of an earlier release is left behind.
#
# To upgrade WAMR, update its entry in third_party/dependencies.json, run this script on a checkout
# of the new release's tag, and derive the file lists and WAMR's definitions in
# cmake/StasisWamr.cmake again. Then check src/wamr_frames.c and the descriptions of traps in
# src/trap.c against the new release's sources.

cmake_minimum_required(VERSION 3.25...4.4)

if(NOT SOURCE)
    message(FATAL_ERROR "Usage: cmake -DSOURCE=<checkout> -P ${CMAKE_CURRENT_LIST_FILE}")
endif()
file(TO_CMAKE_PATH "${SOURCE}" source)
cmake_path(ABSOLUTE_PATH source NORMALIZE)
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH root)
set(destination "${root}/third_party/wamr")

# The release, from WAMR's entry in the manifest.
file(READ "${root}/third_party/dependencies.json" manifest)
string(JSON count LENGTH "${manifest}" dependencies)
set(entry "")
if(count GREATER 0)
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(JSON name GET "${manifest}" dependencies ${index} name)
        if(name STREQUAL "wamr")
            string(JSON entry GET "${manifest}" dependencies ${index})
        endif()
    endforeach()
endif()
if(NOT entry)
    message(FATAL_ERROR "third_party/dependencies.json has no entry for wamr.")
endif()
string(JSON version GET "${entry}" version)
string(JSON commit GET "${entry}" commit)

if(NOT EXISTS "${source}/core/version.h")
    message(FATAL_ERROR "${source} holds no WAMR: it has no core/version.h.")
endif()
file(STRINGS "${source}/core/version.h" lines REGEX "^#define WAMR_VERSION_(MAJOR|MINOR|PATCH) ")
list(TRANSFORM lines REPLACE "^#define WAMR_VERSION_[A-Z]+ +([0-9]+).*$" "\\1")
list(JOIN lines "." found)
if(NOT found STREQUAL version)
    message(FATAL_ERROR
        "${source} holds WAMR ${found}, but third_party/dependencies.json names ${version}."
    )
endif()

find_program(git git REQUIRED)
execute_process(COMMAND "${git}" -C "${source}" rev-parse HEAD
    OUTPUT_VARIABLE head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE error
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${source} isn't a git checkout of WAMR's release:\n${error}")
elseif(NOT head STREQUAL commit)
    message(FATAL_ERROR
        "${source} is at commit ${head}, but third_party/dependencies.json names WAMR ${version} "
        "at ${commit}."
    )
endif()

include("${root}/cmake/StasisWamrFiles.cmake")
set(files
    ${STASIS_WAMR_SOURCES}
    ${STASIS_WAMR_SOURCES_CLASSIC}
    ${STASIS_WAMR_SOURCES_FAST}
    ${STASIS_WAMR_SOURCES_LINUX}
    ${STASIS_WAMR_SOURCES_WINDOWS}
    ${STASIS_WAMR_HEADERS}
    ${STASIS_WAMR_LICENSES}
)
set(missing "")
foreach(path IN LISTS files)
    if(NOT EXISTS "${source}/${path}" OR IS_DIRECTORY "${source}/${path}")
        string(APPEND missing "\n  ${path}")
    endif()
endforeach()
if(missing)
    message(FATAL_ERROR "${source} lacks files that Stasis's build uses:${missing}")
endif()

file(REMOVE_RECURSE "${destination}")
foreach(path IN LISTS files)
    cmake_path(GET path PARENT_PATH directory)
    file(MAKE_DIRECTORY "${destination}/${directory}")
    file(COPY_FILE "${source}/${path}" "${destination}/${path}")
endforeach()
list(LENGTH files copied)
message(STATUS "Copied ${copied} files of WAMR ${version} into ${destination}")
