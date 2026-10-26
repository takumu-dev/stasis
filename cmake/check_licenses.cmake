# Checks the code Stasis's build compiles from elsewhere, vendored under third_party/:
#
#   cmake [-DTHIRD_PARTY=<dir>] [-DNOTICES=<file> | -DWRITE_NOTICES=<file>] -P check_licenses.cmake
#
#   THIRD_PARTY    the directory of vendored code; the third_party/ beside this script by default
#   NOTICES        a third-party notices file that must hold what ships, as WRITE_NOTICES would
#                  write it
#   WRITE_NOTICES  a third-party notices file to write for what ships, once the rest passes
#
# THIRD_PARTY/dependencies.json lists each dependency: its `name`, which is its directory's, its
# `version`, the `url` and `commit` it came from, its `license` as an SPDX expression, whether it
# `ships`, meaning it's compiled into the library a host links, and the licence files that go with
# it as `notices`, paths under its directory. The check fails, naming every problem, unless:
#
# - each directory in THIRD_PARTY holds a dependency, and each dependency has its directory;
# - each dependency that ships has the licence files its `notices` name;
# - each dependency's licence is one THIRD_PARTY/licenses.json allows. That file holds `allowed`,
#   the licences that need no review, each an SPDX licence or `<licence> WITH <exception>`; and
#   `overrides`, each giving a dependency's licence as a review found it, with its `name`, the
#   `version` reviewed, the `license` and the `reason`. Once the dependency's version changes, its
#   override fails the check until it's reviewed again, and the licence an override gives must still
#   be allowed;
# - the notices file, when NOTICES names one, lists each dependency that ships, in the manifest's
#   order, with its licence files, and nothing else.

cmake_minimum_required(VERSION 3.25...4.4)

# Stops with the heading and the problems beneath it, sorted, one to a line.
function(stasis_fail heading)
    set(problems ${ARGN})
    list(SORT problems)
    list(TRANSFORM problems PREPEND "\n  - ")
    list(JOIN problems "" problems)
    message(FATAL_ERROR "${heading}${problems}")
endfunction()

# Sets `variable` to the elements of the JSON array at the path that follows it in `json`, each as
# JSON text, or to nothing if there's no array there.
function(stasis_json_elements variable json)
    set(elements "")
    string(JSON count ERROR_VARIABLE error LENGTH "${json}" ${ARGN})
    if(NOT error AND count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE ${last})
            string(JSON element GET "${json}" ${ARGN} ${index})
            # An element holding a semicolon would split the list.
            string(REPLACE ";" "\\;" element "${element}")
            list(APPEND elements "${element}")
        endforeach()
    endif()
    set(${variable} "${elements}" PARENT_SCOPE)
endfunction()

# Sets `variable` to the text of the licence file at `path` as the notices give it: in a fenced
# block longer than any run of backticks in it, which would close the block early, and without the
# trailing spaces and blank lines around it that a diff would flag.
function(stasis_notice_text variable path)
    file(READ "${path}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(REGEX REPLACE "[ \t]+\n" "\n" text "${text}")
    string(REGEX REPLACE "^\n+" "" text "${text}")
    string(REGEX REPLACE "[ \t\n]+$" "" text "${text}")
    set(fence "```")
    string(REGEX MATCHALL "`+" runs "${text}")
    foreach(run IN LISTS runs)
        string(LENGTH "${run}" length)
        string(LENGTH "${fence}" fence_length)
        if(length GREATER_EQUAL fence_length)
            math(EXPR length "${length} + 1")
            string(REPEAT "`" ${length} fence)
        endif()
    endforeach()
    set(${variable} "${fence}text\n${text}\n${fence}\n" PARENT_SCOPE)
endfunction()

# Sets `variable` to 1 if the licences that follow `expression`, the allowlist, cover that SPDX
# licence expression, and to 0 otherwise. An AND needs every side covered and an OR any one, with
# AND binding tighter. A licence
# with an exception, `<licence> WITH <exception>`, is covered only if the allowlist names it so. An
# expression that can't be read isn't covered.
function(stasis_license_allowed variable expression)
    set(allowed ${ARGN})
    string(REPLACE "(" " ( " expression "${expression}")
    string(REPLACE ")" " ) " expression "${expression}")
    string(STRIP "${expression}" expression)
    string(REGEX REPLACE "[ \t\n]+" ";" tokens "${expression}")

    # With each licence, and its exception, replaced by whether it's allowed, only AND, OR and
    # parentheses are left to evaluate.
    set(values "")
    list(LENGTH tokens count)
    set(index 0)
    while(index LESS count)
        list(GET tokens ${index} token)
        math(EXPR next "${index} + 1")
        if(token MATCHES "^(AND|OR|\\(|\\))$")
            list(APPEND values "${token}")
        elseif(token STREQUAL "WITH")
            set(${variable} 0 PARENT_SCOPE)
            return()
        else()
            set(license "${token}")
            if(next LESS count)
                list(GET tokens ${next} following)
                if(following STREQUAL "WITH")
                    math(EXPR exception "${next} + 1")
                    if(exception GREATER_EQUAL count)
                        set(${variable} 0 PARENT_SCOPE)
                        return()
                    endif()
                    list(GET tokens ${exception} exception)
                    string(APPEND license " WITH ${exception}")
                    math(EXPR next "${next} + 2")
                endif()
            endif()
            if(license IN_LIST allowed)
                list(APPEND values 1)
            else()
                list(APPEND values 0)
            endif()
        endif()
        set(index ${next})
    endwhile()
    list(JOIN values " " values)

    # CMake's regular expressions can't match nested parentheses, so the innermost groups go first.
    while(values MATCHES "\\(([^()]*)\\)")
        set(group "${CMAKE_MATCH_0}")
        stasis_flat_license_value(value "${CMAKE_MATCH_1}")
        if(value STREQUAL "")
            set(${variable} 0 PARENT_SCOPE)
            return()
        endif()
        string(REPLACE "${group}" "${value}" values "${values}")
    endwhile()
    stasis_flat_license_value(value "${values}")
    if(value STREQUAL "")
        set(value 0)
    endif()
    set(${variable} ${value} PARENT_SCOPE)
endfunction()

# Sets `variable` to the value of a run of 1s and 0s joined by AND and OR, with no parentheses, or
# to nothing if the run doesn't read as one.
function(stasis_flat_license_value variable values)
    string(STRIP "${values}" values)
    if(NOT values MATCHES "^[01]( (AND|OR) [01])*$")
        set(${variable} "" PARENT_SCOPE)
        return()
    endif()
    string(REPLACE " OR " ";" alternatives "${values}")
    foreach(alternative IN LISTS alternatives)
        if(NOT alternative MATCHES "0")
            set(${variable} 1 PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${variable} 0 PARENT_SCOPE)
endfunction()

if(NOT DEFINED THIRD_PARTY)
    cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH root)
    set(THIRD_PARTY "${root}/third_party")
endif()

file(READ "${THIRD_PARTY}/licenses.json" licenses)
stasis_json_elements(allowed "${licenses}" allowed)

# CMake has no maps, so each override is looked up by its dependency's name, in
# override_license_<name> and override_version_<name>.
stasis_json_elements(overrides "${licenses}" overrides)
set(index 0)
foreach(override IN LISTS overrides)
    string(JSON name ERROR_VARIABLE error GET "${override}" name)
    if(error)
        message(FATAL_ERROR "Override ${index} in licenses.json names no dependency.")
    endif()
    foreach(field IN ITEMS version license reason)
        string(JSON ${field} ERROR_VARIABLE error GET "${override}" ${field})
        if(error OR ${field} STREQUAL "")
            message(FATAL_ERROR "The override for ${name} in licenses.json lacks its ${field}.")
        endif()
    endforeach()
    set(override_version_${name} "${version}")
    set(override_license_${name} "${license}")
    math(EXPR index "${index} + 1")
endforeach()

file(READ "${THIRD_PARTY}/dependencies.json" manifest)
stasis_json_elements(dependencies "${manifest}" dependencies)

set(problems "")
set(names "")
set(shipped "")
foreach(dependency IN LISTS dependencies)
    string(JSON name GET "${dependency}" name)
    string(JSON version GET "${dependency}" version)
    string(JSON license GET "${dependency}" license)
    list(APPEND names "${name}")
    if(NOT IS_DIRECTORY "${THIRD_PARTY}/${name}")
        list(APPEND problems "${name} ${version} has no directory of vendored code.")
    endif()

    # What the notices say of a dependency that ships is kept as entry_<name>.
    string(JSON ships GET "${dependency}" ships)
    if(ships)
        list(APPEND shipped "${name}")
        string(JSON url GET "${dependency}" url)
        string(JSON commit GET "${dependency}" commit)
        set(entry_${name} "\n## ${name}\n\n- Version: ${version}\n- Licence: ${license}\n")
        string(APPEND entry_${name} "- Source: ${url}, at commit ${commit}\n")
        set(version_${name} "${version}")
        stasis_json_elements(notices "${dependency}" notices)
        if(NOT notices)
            list(APPEND problems
                "${name} ${version} ships, but names no licence files to go with it."
            )
        endif()
        foreach(notice IN LISTS notices)
            if(NOT EXISTS "${THIRD_PARTY}/${name}/${notice}")
                list(APPEND problems
                    "${name} ${version} lacks ${notice}, a licence file that goes with it."
                )
                continue()
            endif()
            stasis_notice_text(text "${THIRD_PARTY}/${name}/${notice}")
            string(APPEND entry_${name} "\n### ${notice}\n\n${text}")
        endforeach()
    endif()

    set(by_override "")
    if(DEFINED override_license_${name})
        if(NOT version STREQUAL override_version_${name})
            list(APPEND problems
                "${name} ${version} isn't the version its override was reviewed at, \
${override_version_${name}}. Review its licence again, and update the override."
            )
            continue()
        endif()
        set(license "${override_license_${name}}")
        set(by_override " by its override")
    endif()
    stasis_license_allowed(covered "${license}" ${allowed})
    if(NOT covered)
        list(APPEND problems "${name} ${version} is under ${license}${by_override}, which the \
allowlist doesn't cover."
        )
    endif()
endforeach()

file(GLOB entries LIST_DIRECTORIES true RELATIVE "${THIRD_PARTY}" "${THIRD_PARTY}/*")
foreach(entry IN LISTS entries)
    if(IS_DIRECTORY "${THIRD_PARTY}/${entry}" AND NOT entry IN_LIST names)
        list(APPEND problems
            "${entry} is a directory of vendored code that dependencies.json doesn't name."
        )
    endif()
endforeach()

if(problems)
    stasis_fail("The vendored dependencies fail the licence check:" ${problems})
endif()

if(NOT DEFINED NOTICES AND NOT DEFINED WRITE_NOTICES)
    return()
endif()

set(notices [[
# Third-party notices

Stasis compiles the code of the projects below into its library, so whatever ships Stasis ships
their code too. Each is listed with its version, its licence as an SPDX expression and where it
comes from, followed by the licence files that come with it, which its licence asks to be passed on.

This file is written from `third_party/dependencies.json` and the licence files it names. To bring
it up to date, run `cmake -DWRITE_NOTICES=THIRD_PARTY_NOTICES.md -P cmake/check_licenses.cmake` in
the source tree rather than editing it.
]])
foreach(name IN LISTS shipped)
    string(APPEND notices "${entry_${name}}")
endforeach()
if(NOT shipped)
    string(APPEND notices "\nNo third-party code is compiled into Stasis's library.\n")
endif()

if(DEFINED WRITE_NOTICES)
    file(WRITE "${WRITE_NOTICES}" "${notices}")
    return()
endif()

set(existing "")
if(EXISTS "${NOTICES}")
    file(READ "${NOTICES}" existing)
    string(REPLACE "\r\n" "\n" existing "${existing}")
endif()
if(existing STREQUAL notices)
    return()
endif()
cmake_path(GET NOTICES FILENAME notices_name)
set(problems "")
foreach(name IN LISTS shipped)
    string(FIND "${existing}" "${entry_${name}}" found)
    if(NOT found EQUAL -1)
        continue()
    endif()
    string(FIND "${existing}" "\n## ${name}\n\n- Version: " found)
    if(found EQUAL -1)
        list(APPEND problems "${notices_name} lacks ${name} ${version_${name}}, which ships.")
    else()
        list(APPEND problems "${notices_name} is out of date for ${name} ${version_${name}}.")
    endif()
endforeach()
string(REGEX MATCHALL "\n## [^\n]+\n\n- Version: [^\n]+" headings "${existing}")
foreach(heading IN LISTS headings)
    string(REGEX MATCH "^\n## ([^\n]+)\n\n- Version: (.+)$" _ "${heading}")
    if(NOT CMAKE_MATCH_1 IN_LIST shipped)
        list(APPEND problems
            "${notices_name} lists ${CMAKE_MATCH_1} ${CMAKE_MATCH_2}, which doesn't ship."
        )
    endif()
endforeach()
if(NOT problems)
    list(APPEND problems "${notices_name} differs outside the dependencies' entries.")
endif()
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH root)
cmake_path(RELATIVE_PATH CMAKE_CURRENT_LIST_FILE BASE_DIRECTORY "${root}" OUTPUT_VARIABLE script)
stasis_fail("${notices_name} doesn't match what ships. Run cmake -DWRITE_NOTICES=${notices_name} \
-P ${script} to write it again:" ${problems}
)
