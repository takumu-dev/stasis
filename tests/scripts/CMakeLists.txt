# The CTest tests scripts.*, which build one script of each kind from source and check that
# wasm-tools finds it valid. A C script is valid with the features of Lime1 alone, the target its
# flags name.

stasis_add_wat_module(smoke_wat smoke.wat)
add_test(NAME scripts.build_a_valid_wat_module
    COMMAND "${STASIS_WASM_TOOLS}" validate "${STASIS_SCRIPTS_DIR}/smoke_wat.wasm"
)

stasis_add_c_script(smoke_c smoke.c)
add_test(NAME scripts.build_a_valid_c_script
    COMMAND "${STASIS_WASM_TOOLS}" validate --features lime1 "${STASIS_SCRIPTS_DIR}/smoke_c.wasm"
)
