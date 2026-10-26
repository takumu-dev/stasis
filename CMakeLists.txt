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

# The library: static unless BUILD_SHARED_LIBS asks for a shared one. It exports only what
# STASIS_API marks, and a host linking it shared gets STASIS_SHARED, which makes STASIS_API import
# the functions on Windows.
add_library(stasis)
add_library(stasis::stasis ALIAS stasis)
target_sources(stasis
    PRIVATE
        src/status.c
        $<TARGET_OBJECTS:stasis_sha256>
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/stasis.h
)
set_target_properties(stasis PROPERTIES DEFINE_SYMBOL STASIS_EXPORTS)
get_target_property(stasis_type stasis TYPE)
if(stasis_type STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(stasis INTERFACE STASIS_SHARED)
endif()
stasis_target_conventions(stasis)

if(STASIS_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS stasis FILE_SET HEADERS)
endif()

if(STASIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(STASIS_BUILD_TOOLS)
    include(StasisLint)
endif()
