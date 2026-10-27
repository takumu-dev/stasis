# stasis-gen, the generator: a program of Stasis's own C, which shares the library's SHA-256 and
# encoding of signatures.

add_executable(stasis_generator
    arena.c
    arena.h
    c_names.c
    c_names.h
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
    parser.c
    parser.h
    signatures.c
    signatures.h
    text.c
    text.h
)
add_executable(stasis::generator ALIAS stasis_generator)
target_link_libraries(stasis_generator PRIVATE stasis_signature stasis_sha256)
target_compile_definitions(stasis_generator PRIVATE
    "STASIS_VERSION=\"${PROJECT_VERSION}\""
    "STASIS_HOST_FUNCTION_STACK=${STASIS_WAMR_STACK_GUARD_SIZE}"
)
set_target_properties(stasis_generator PROPERTIES OUTPUT_NAME stasis-gen)
stasis_target_conventions(stasis_generator)
