# The CTest tests licenses.*, which run the licence check on third_party/ trees of their own.

set(license_check "${PROJECT_SOURCE_DIR}/cmake/check_licenses.cmake")

# Adds the CTest test licenses.<name>, which runs the licence check on a third_party/ tree written
# into the build tree. MANIFEST gives its dependencies.json, and ALLOWLIST its licenses.json, which
# is Stasis's own unless given. Each dependency the manifest names has a directory holding the
# files its `notices` name, each with the text "<path> of <dependency>". MISSING leaves out a
# dependency's directory, given as its name, or one of its files, given as <name>/<path>, and EXTRA
# names directories that no dependency names.
#
# NOTICES names a notices file under notices/ for the check to compare with what ships, and WRITES
# a notices file for it to write instead. The check must pass, or fail with output that matches
# FAILS_WITH.
function(stasis_add_license_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg ""
        "MANIFEST;ALLOWLIST;NOTICES;WRITES;FAILS_WITH" "MISSING;EXTRA"
    )
    set(tree "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    file(REMOVE_RECURSE "${tree}")
    file(WRITE "${tree}/dependencies.json" "${arg_MANIFEST}")
    if(arg_ALLOWLIST)
        file(WRITE "${tree}/licenses.json" "${arg_ALLOWLIST}")
    else()
        file(COPY_FILE "${PROJECT_SOURCE_DIR}/third_party/licenses.json" "${tree}/licenses.json")
    endif()

    string(JSON count LENGTH "${arg_MANIFEST}" dependencies)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE ${last})
            string(JSON dependency GET "${arg_MANIFEST}" dependencies ${index} name)
            if(dependency IN_LIST arg_MISSING)
                continue()
            endif()
            file(MAKE_DIRECTORY "${tree}/${dependency}")
            string(JSON notices ERROR_VARIABLE error
                GET "${arg_MANIFEST}" dependencies ${index} notices
            )
            if(error)
                continue()
            endif()
            string(JSON notice_count LENGTH "${notices}")
            if(notice_count EQUAL 0)
                continue()
            endif()
            math(EXPR last_notice "${notice_count} - 1")
            foreach(notice_index RANGE ${last_notice})
                string(JSON notice GET "${notices}" ${notice_index})
                if(NOT "${dependency}/${notice}" IN_LIST arg_MISSING)
                    file(WRITE "${tree}/${dependency}/${notice}" "${notice} of ${dependency}\n")
                endif()
            endforeach()
        endforeach()
    endif()
    foreach(directory IN LISTS arg_EXTRA)
        file(MAKE_DIRECTORY "${tree}/${directory}")
    endforeach()

    set(options "-DTHIRD_PARTY=${tree}")
    if(arg_NOTICES)
        list(APPEND options "-DNOTICES=${CMAKE_CURRENT_SOURCE_DIR}/notices/${arg_NOTICES}")
    elseif(arg_WRITES)
        list(APPEND options "-DWRITE_NOTICES=${arg_WRITES}")
    endif()
    add_test(NAME licenses.${name}
        COMMAND "${CMAKE_COMMAND}" ${options} -P "${license_check}"
    )
    if(arg_FAILS_WITH)
        set_tests_properties(licenses.${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "${arg_FAILS_WITH}"
        )
    endif()
endfunction()

stasis_add_license_test(fails_a_licence_outside_the_allowlist
    MANIFEST [[{"dependencies": [
        {"name": "readline", "version": "8.2", "license": "GPL-3.0-or-later", "ships": false},
        {"name": "zlib", "version": "1.3.2", "license": "Zlib", "ships": false}
    ]}]]
    FAILS_WITH "readline 8\\.2 is under GPL-3\\.0-or-later, which the allowlist doesn't cover\\."
)
stasis_add_license_test(names_every_dependency_it_fails
    MANIFEST [[{"dependencies": [
        {"name": "readline", "version": "8.2", "license": "GPL-3.0-or-later", "ships": false},
        {"name": "libiconv", "version": "1.18", "license": "LGPL-2.1-or-later", "ships": false}
    ]}]]
    FAILS_WITH "\n +- libiconv 1\\.18 [^\n]*\n +- readline 8\\.2 "
)
stasis_add_license_test(passes_licences_the_allowlist_covers
    MANIFEST [[{"dependencies": [
        {"name": "wamr", "version": "2.4.5", "ships": false,
         "license": "Apache-2.0 WITH LLVM-exception AND Apache-2.0"},
        {"name": "mimalloc", "version": "3.1.5", "license": "GPL-3.0-only OR MIT", "ships": false},
        {"name": "sdl", "version": "3.4.18", "license": "(Zlib OR GPL-3.0-only) AND (MIT AND ISC)",
         "ships": false}
    ]}]]
)
stasis_add_license_test(fails_a_licence_with_an_exception_the_allowlist_doesnt_name
    MANIFEST [[{"dependencies": [
        {"name": "jdk", "version": "21", "license": "Apache-2.0 WITH Classpath-exception-2.0",
         "ships": false}
    ]}]]
    FAILS_WITH "jdk 21 is under Apache-2\\.0 WITH Classpath-exception-2\\.0, which the allowlist"
)
stasis_add_license_test(fails_licences_joined_by_and_unless_the_allowlist_covers_each
    MANIFEST [[{"dependencies": [
        {"name": "qhull", "version": "8.0.2", "license": "MIT AND GPL-3.0-only", "ships": false}
    ]}]]
    FAILS_WITH "qhull 8\\.0\\.2 is under MIT AND GPL-3\\.0-only, which the allowlist doesn't cover"
)
stasis_add_license_test(fails_a_licence_it_cant_read
    MANIFEST [[{"dependencies": [
        {"name": "odd", "version": "1", "license": "MIT AND (Zlib", "ships": false}
    ]}]]
    FAILS_WITH "odd 1 is under MIT AND \\(Zlib, which the allowlist doesn't cover"
)

# An override gives a dependency's licence as a review found it, for the version reviewed.
set(override_allowlist [[{
    "allowed": ["MIT"],
    "overrides": [
        {"name": "fonts", "version": "2.0", "license": "MIT",
         "reason": "Its manifest says OFL-1.1, but the files vendored are MIT."}
    ]
}]])
stasis_add_license_test(passes_the_licence_an_override_gives
    MANIFEST [[{"dependencies": [
        {"name": "fonts", "version": "2.0", "license": "OFL-1.1", "ships": false}
    ]}]]
    ALLOWLIST "${override_allowlist}"
)
stasis_add_license_test(fails_an_override_for_another_version
    MANIFEST [[{"dependencies": [
        {"name": "fonts", "version": "2.1", "license": "OFL-1.1", "ships": false}
    ]}]]
    ALLOWLIST "${override_allowlist}"
    FAILS_WITH "fonts 2\\.1 isn't the version its override was reviewed at, 2\\.0\\."
)
stasis_add_license_test(fails_an_override_whose_licence_the_allowlist_doesnt_cover
    MANIFEST [[{"dependencies": [
        {"name": "fonts", "version": "2.0", "license": "OFL-1.1", "ships": false}
    ]}]]
    ALLOWLIST [[{
        "allowed": ["Zlib"],
        "overrides": [{"name": "fonts", "version": "2.0", "license": "MIT", "reason": "Reviewed."}]
    }]]
    FAILS_WITH "fonts 2\\.0 is under MIT by its override, which the allowlist doesn't cover\\."
)
stasis_add_license_test(fails_an_override_without_a_reason
    MANIFEST [[{"dependencies": [
        {"name": "fonts", "version": "2.0", "license": "OFL-1.1", "ships": false}
    ]}]]
    ALLOWLIST [[{
        "allowed": ["MIT"],
        "overrides": [{"name": "fonts", "version": "2.0", "license": "MIT"}]
    }]]
    FAILS_WITH "The override for fonts in licenses\\.json lacks its reason\\."
)

# Every directory holds a dependency the manifest names, and every dependency has its directory.
stasis_add_license_test(fails_a_directory_no_dependency_names
    MANIFEST [[{"dependencies": [
        {"name": "zlib", "version": "1.3.2", "license": "Zlib", "ships": false}
    ]}]]
    EXTRA lz4
    FAILS_WITH "lz4 is a directory of vendored code that dependencies\\.json doesn't name\\."
)
stasis_add_license_test(fails_a_dependency_without_its_directory
    MANIFEST [[{"dependencies": [
        {"name": "zlib", "version": "1.3.2", "license": "Zlib", "ships": false},
        {"name": "lz4", "version": "1.10.0", "license": "BSD-2-Clause", "ships": false}
    ]}]]
    MISSING lz4
    FAILS_WITH "lz4 1\\.10\\.0 has no directory of vendored code\\."
)

# The notices file holds each dependency that ships, and nothing else.
# notices/THIRD_PARTY_NOTICES.md is written by hand, for lz4 and zlib.
set(lz4 [[{"name": "lz4", "version": "1.10.0", "url": "https://github.com/lz4/lz4",
    "commit": "ebb370ce83d00a6e5e8ba0bd0a3a9e6c3c6b1f8a", "license": "BSD-2-Clause",
    "ships": true, "notices": ["LICENSE"]}]])
set(zlib [[{"name": "zlib", "version": "1.3.2", "url": "https://github.com/madler/zlib",
    "commit": "51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf", "license": "Zlib",
    "ships": true, "notices": ["LICENSE", "contrib/README"]}]])
set(gtest [[{"name": "googletest", "version": "1.17.0",
    "url": "https://github.com/google/googletest",
    "commit": "52eb8108c5bdec04579160ae17225d66034bd723", "license": "BSD-3-Clause", "ships": false, "notices": ["LICENSE"]}]])
stasis_add_license_test(passes_notices_of_what_ships
    MANIFEST "{\"dependencies\": [${lz4}, ${zlib}, ${gtest}]}"
    NOTICES THIRD_PARTY_NOTICES.md
)
set(lua [[{"name": "lua", "version": "5.4.8", "url": "https://github.com/lua/lua",
    "commit": "1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3", "license": "MIT",
    "ships": true, "notices": ["LICENSE"]}]])
stasis_add_license_test(fails_notices_that_lack_a_dependency_that_ships
    MANIFEST "{\"dependencies\": [${lz4}, ${zlib}, ${lua}]}"
    NOTICES THIRD_PARTY_NOTICES.md
    FAILS_WITH "THIRD_PARTY_NOTICES\\.md lacks lua 5\\.4\\.8, which ships\\."
)
set(zlib_unshipped [[{"name": "zlib", "version": "1.3.2", "url": "https://github.com/madler/zlib",
    "commit": "51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf", "license": "Zlib",
    "ships": false, "notices": ["LICENSE", "contrib/README"]}]])
stasis_add_license_test(fails_notices_that_list_a_dependency_that_doesnt_ship
    MANIFEST "{\"dependencies\": [${lz4}, ${zlib_unshipped}]}"
    NOTICES THIRD_PARTY_NOTICES.md
    FAILS_WITH "THIRD_PARTY_NOTICES\\.md lists zlib 1\\.3\\.2, which doesn't ship\\."
)
string(REPLACE "1.10.0" "1.10.1" lz4_newer "${lz4}")
stasis_add_license_test(fails_notices_out_of_date_for_a_dependency
    MANIFEST "{\"dependencies\": [${lz4_newer}, ${zlib}]}"
    NOTICES THIRD_PARTY_NOTICES.md
    FAILS_WITH "THIRD_PARTY_NOTICES\\.md is out of date for lz4 1\\.10\\.1\\."
)
stasis_add_license_test(fails_a_dependency_that_ships_without_one_of_its_notices
    MANIFEST "{\"dependencies\": [${lz4}, ${zlib}]}"
    MISSING zlib/contrib/README
    FAILS_WITH "zlib 1\\.3\\.2 lacks contrib/README, a licence file that goes with it\\."
)
set(lua_without_notices [[{"name": "lua", "version": "5.4.8", "url": "https://github.com/lua/lua",
    "commit": "1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3", "license": "MIT", "ships": true}]])
stasis_add_license_test(fails_a_dependency_that_ships_without_notices
    MANIFEST "{\"dependencies\": [${lua_without_notices}]}"
    FAILS_WITH "lua 5\\.4\\.8 ships, but names no licence files to go with it\\."
)

# Writing the notices gives the file written by hand. CMake's file(WRITE) ends lines in CRLF on
# Windows, and the file written by hand is checked out with LF.
set(written "${CMAKE_CURRENT_BINARY_DIR}/writes_notices_of_what_ships.md")
stasis_add_license_test(writes_notices_of_what_ships
    MANIFEST "{\"dependencies\": [${gtest}, ${lz4}, ${zlib}]}"
    WRITES "${written}"
)
set_tests_properties(licenses.writes_notices_of_what_ships PROPERTIES
    FIXTURES_SETUP license_notices_written
)
add_test(NAME licenses.writes_notices_of_what_ships_as_expected
    COMMAND "${CMAKE_COMMAND}" -E compare_files --ignore-eol
        "${written}" "${CMAKE_CURRENT_SOURCE_DIR}/notices/THIRD_PARTY_NOTICES.md"
)
set_tests_properties(licenses.writes_notices_of_what_ships_as_expected PROPERTIES
    FIXTURES_REQUIRED license_notices_written
)
