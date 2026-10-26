# Stasis: a game scripting library that runs scripts compiled to WebAssembly in a sandbox inside a
# game, and saves and restores their state as snapshots.
#
# The library builds with a C compiler alone, and the build never downloads anything. Every
# configuration Stasis builds itself is a preset in CMakePresets.json.
cmake_minimum_required(VERSION 3.25...4.4)

project(stasis
    VERSION 0.1.0
    DESCRIPTION "A game scripting library that runs WebAssembly scripts and snapshots their state"
    HOMEPAGE_URL "https://github.com/takumu-dev/stasis"
    LANGUAGES C
)

# Stasis builds for x86-64 Linux and Windows only, and configuring for anything else stops here
# rather than somewhere in the build. cl and clang-cl report the architecture they compile for,
# where CMAKE_SYSTEM_PROCESSOR names the host's.
if(MSVC)
    set(stasis_processor "${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
else()
    set(stasis_processor "${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows)$"
    OR NOT stasis_processor MATCHES "^(x86_64|AMD64|x64)$"
    OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
)
    message(FATAL_ERROR
        "Stasis builds for x86-64 Linux and Windows only; this is ${CMAKE_SYSTEM_NAME} on "
        "${stasis_processor}, with ${CMAKE_SIZEOF_VOID_P}-byte pointers."
    )
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(StasisConventions)

# The licence check, of the code vendored under third_party/: each dependency's licence against the
# allowlist, each directory against the manifest, and THIRD_PARTY_NOTICES.md against what ships. It
# runs whenever Stasis is the top-level project, and never in a host's build.
if(PROJECT_IS_TOP_LEVEL)
    set(stasis_license_check "${PROJECT_SOURCE_DIR}/cmake/check_licenses.cmake")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DNOTICES=${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            -P "${stasis_license_check}"
        RESULT_VARIABLE stasis_license_result
    )
    if(NOT stasis_license_result EQUAL 0)
        message(FATAL_ERROR "The licence check above failed.")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${stasis_license_check}"
        "${PROJECT_SOURCE_DIR}/third_party/dependencies.json"
        "${PROJECT_SOURCE_DIR}/third_party/licenses.json"
        "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    )
endif()

# A host that adds Stasis as a subproject gets the library alone.
option(STASIS_BUILD_TESTS "Build Stasis's tests." ${PROJECT_IS_TOP_LEVEL})
option(STASIS_BUILD_TOOLS "Add the targets that format and check Stasis's sources."
    ${PROJECT_IS_TOP_LEVEL}
)
option(STASIS_INSTALL "Install Stasis's library and public header." ${PROJECT_IS_TOP_LEVEL})
option(STASIS_UBSAN
    "Build Stasis's own code with UndefinedBehaviorSanitizer, as the sanitize preset does."
    OFF
)
option(STASIS_CLANG_TIDY
    "Run wasi-sdk's clang-tidy on Stasis's own code as it builds, as the tidy preset does."
    OFF
)
set(STASIS_WAMR_INTERPRETER fast CACHE STRING
    "The WebAssembly interpreter scripts run on: fast, or classic, whose trap reports give the \
offsets in a script's module that map to its source lines."
)
set_property(CACHE STASIS_WAMR_INTERPRETER PROPERTY STRINGS fast classic)
if(NOT STASIS_WAMR_INTERPRETER MATCHES "^(fast|classic)$")
    message(FATAL_ERROR
        "STASIS_WAMR_INTERPRETER is ${STASIS_WAMR_INTERPRETER}, but it can only be fast or classic."
    )
endif()

# clang-tidy comes from the pinned wasi-sdk, so every machine checks the code alike. wasi-sdk's LLVM
# targets WebAssembly alone, so its clang-tidy is told the target Stasis builds for, and then reads
# this machine's headers.
if(STASIS_CLANG_TIDY)
    include(StasisWasiSdk)
    stasis_wasi_sdk_program(stasis_clang_tidy clang-tidy REQUIRED_BY "STASIS_CLANG_TIDY")
    set(CMAKE_C_CLANG_TIDY
        "${stasis_clang_tidy}"
        --extra-arg=--target=x86_64-linux-gnu
        --warnings-as-errors=*
    )
endif()

# A program on Windows finds a DLL beside it, so Stasis's own build puts the shared library and the
# tests in one directory.
if(PROJECT_IS_TOP_LEVEL AND NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

# SHA-256, compiled once for the library and for anything else of Stasis's that hashes. It goes
# into the shared library too, so it's position-independent.
add_library(stasis_sha256 OBJECT src/sha256.c src/sha256.h)
target_include_directories(stasis_sha256 PUBLIC "${PROJECT_SOURCE_DIR}/src")
set_target_properties(stasis_sha256 PROPERTIES POSITION_INDEPENDENT_CODE ON)
stasis_target_conventions(stasis_sha256)

# WAMR, compiled into the library, with its printing discarded.
include(StasisWamr)
stasis_add_wamr(stasis_wamr VPRINTF stasis_wamr_vprintf)

# The core's own sources, which the tests' test-only library compiles again.
set(STASIS_CORE_SOURCES
    "${PROJECT_SOURCE_DIR}/src/status.c"
    "${PROJECT_SOURCE_DIR}/src/wamr_hooks.c"
)

# The library: static unless BUILD_SHARED_LIBS asks for a shared one, with WAMR inside it either
# way. It exports only what STASIS_API marks, and a host linking it shared gets STASIS_SHARED, which
# makes STASIS_API import the functions on Windows.
add_library(stasis)
add_library(stasis::stasis ALIAS stasis)
target_sources(stasis
    PRIVATE
        ${STASIS_CORE_SOURCES}
        $<TARGET_OBJECTS:stasis_sha256>
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/stasis.h
)
stasis_target_holds_wamr(stasis stasis_wamr)
set_target_properties(stasis PROPERTIES DEFINE_SYMBOL STASIS_EXPORTS)
get_target_property(stasis_type stasis TYPE)
if(stasis_type STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(stasis INTERFACE STASIS_SHARED)
    # On Linux, a version script keeps WAMR's assembly, which hidden visibility can't reach, from
    # being exported too.
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(stasis_exports "${PROJECT_SOURCE_DIR}/src/exports.map")
        target_link_options(stasis PRIVATE "LINKER:--version-script=${stasis_exports}")
        set_property(TARGET stasis APPEND PROPERTY LINK_DEPENDS "${stasis_exports}")
    endif()
endif()
stasis_target_conventions(stasis)

# The notices of the third-party code compiled into the library go with it.
if(STASIS_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS stasis FILE_SET HEADERS)
    install(FILES THIRD_PARTY_NOTICES.md TYPE DOC)
endif()

if(STASIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(STASIS_BUILD_TOOLS)
    include(StasisLint)
endif()
