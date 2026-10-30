# stasis-gen, the generator: a program of Stasis's own C, which shares the library's SHA-256 and
# encoding of signatures.

# The licence notices that the C bindings put into every C script are kept as text, and compiled
# into stasis-gen as bytes, which spares them escaping.
set(stasis_notices "${CMAKE_CURRENT_SOURCE_DIR}/wasi_libc_notices.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${stasis_notices}")
file(READ "${stasis_notices}" stasis_notices_hex HEX)
string(LENGTH "${stasis_notices_hex}" stasis_notices_size)
math(EXPR stasis_notices_size "${stasis_notices_size} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," stasis_notices_bytes "${stasis_notices_hex}")
string(REGEX REPLACE "((0x..,){16})" "\\1\n    " stasis_notices_bytes "${stasis_notices_bytes}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wasi_libc_notices.c" CONTENT [[
/* Generated from wasi_libc_notices.txt by the build. Do not edit. */

#include "notices.h"

#include <stddef.h>

const unsigned char wasi_libc_notices[] = {
    @stasis_notices_bytes@
};

const size_t wasi_libc_notices_size = @stasis_notices_size@;
]] @ONLY)

add_executable(stasis_generator
    arena.c
    arena.h
    c_bindings.c
    c_bindings.h
    c_names.c
    c_names.h
    c_writer.c
    c_writer.h
    checker.c
    checker.h
    diagnostics.c
    diagnostics.h
    files.c
    files.h
    host.c
    host.h
    interface.c
    interface.h
    lexer.c
    lexer.h
    main.c
    notices.h
    parser.c
    parser.h
    signatures.c
    signatures.h
    text.c
    text.h
    wit.c
    wit.h
    "${CMAKE_CURRENT_BINARY_DIR}/wasi_libc_notices.c"
)
add_executable(stasis::generator ALIAS stasis_generator)
target_include_directories(stasis_generator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(stasis_generator PRIVATE stasis_signature stasis_sha256)
target_compile_definitions(stasis_generator PRIVATE
    "STASIS_VERSION=\"${PROJECT_VERSION}\""
    "STASIS_HOST_FUNCTION_STACK=${STASIS_HOST_FUNCTION_STACK}"
)
set_target_properties(stasis_generator PROPERTIES OUTPUT_NAME stasis-gen)
stasis_target_conventions(stasis_generator)
