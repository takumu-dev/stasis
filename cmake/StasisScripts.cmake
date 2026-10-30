# The scripts Stasis's tests run, built from source so that no compiled WebAssembly is committed:
# WAT modules with the pinned wasm-tools, and C scripts with the pinned wasi-sdk's clang. Including
# this finds both tools, and stops the configuration if either is missing.
#
# Each script is built as STASIS_SCRIPTS_DIR/<name>.wasm, by the target stasis_script_<name>, which
# every build makes.

include_guard(GLOBAL)

include(StasisPins)
include(StasisWasiSdk)

# Accepts only the pinned wasm-tools, which prints "wasm-tools <version> (<commit> <date>)".
function(stasis_is_pinned_wasm_tools result program)
    stasis_pinned_version(pinned wasm-tools)
    execute_process(COMMAND "${program}" --version
        OUTPUT_VARIABLE output
        ERROR_QUIET
        RESULT_VARIABLE status
    )
    string(FIND "${output}" "wasm-tools ${pinned} " position)
    if(NOT status EQUAL 0 OR NOT position EQUAL 0)
        set(${result} FALSE PARENT_SCOPE)
    endif()
endfunction()

find_program(STASIS_WASM_TOOLS wasm-tools
    VALIDATOR stasis_is_pinned_wasm_tools
    DOC "The pinned wasm-tools, which builds WAT modules and validates scripts"
)
if(NOT STASIS_WASM_TOOLS)
    stasis_pinned_version(stasis_wasm_tools_version wasm-tools)
    message(FATAL_ERROR
        "Stasis's tests need wasm-tools ${stasis_wasm_tools_version}, which isn't on the PATH. "
        "tools/install.cmake installs it, or STASIS_WASM_TOOLS can name the program."
    )
endif()

stasis_wasi_sdk_program(STASIS_WASI_CLANG clang REQUIRED_BY "Building the tests' C scripts")
stasis_wasi_sdk_path(STASIS_WASI_SYSROOT share/wasi-sysroot
    REQUIRED_BY "Building the tests' C scripts"
)

set(STASIS_SCRIPTS_DIR "${PROJECT_BINARY_DIR}/scripts")
file(MAKE_DIRECTORY "${STASIS_SCRIPTS_DIR}")

# The flags every C script is built with: for compiling and linking, then for linking alone. Paths
# in the debug information and in __FILE__ are relative to the source tree, so a script builds to
# the same bytes wherever the tree is.
set(STASIS_C_SCRIPT_FLAGS
    --target=wasm32-wasip1
    -mcpu=lime1
    -g
    "-ffile-prefix-map=${PROJECT_SOURCE_DIR}/="
    -fdebug-compilation-dir=.
    -Werror=date-time
)
set(STASIS_C_SCRIPT_LINK_FLAGS
    -nostartfiles
    -Wl,--no-entry
    -Wl,--export=__stack_pointer
    --no-wasm-opt
)

# Builds the WAT module `source` as the script <name>.wasm.
function(stasis_add_wat_module name source)
    cmake_path(ABSOLUTE_PATH source NORMALIZE)
    set(module "${STASIS_SCRIPTS_DIR}/${name}.wasm")
    add_custom_command(OUTPUT "${module}"
        COMMAND "${STASIS_WASM_TOOLS}" parse "${source}" -o "${module}"
        DEPENDS "${source}"
        COMMENT "Building the WAT module ${name}.wasm"
        VERBATIM
    )
    add_custom_target(stasis_script_${name} ALL DEPENDS "${module}")
endfunction()

# Builds the C script <name>.wasm from the C sources that follow the name, compiling each on its own
# and linking them with the C library, with the flags above and nothing else but the directories
# that INCLUDE_DIRECTORIES names, which go on the include path. A source may be generated, such as
# the C bindings that stasis_generate writes, in which case DEPENDS names the files it's generated
# with, which each source is compiled after.
#
#   stasis_add_c_script(<name> <source>... [INCLUDE_DIRECTORIES <directory>...]
#                       [DEPENDS <file>...])
function(stasis_add_c_script name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "INCLUDE_DIRECTORIES;DEPENDS")
    set(script "${STASIS_SCRIPTS_DIR}/${name}.wasm")
    set(objects_dir "${CMAKE_CURRENT_BINARY_DIR}/stasis_script_${name}.dir")
    file(MAKE_DIRECTORY "${objects_dir}")
    set(sysroot "--sysroot=${STASIS_WASI_SYSROOT}")
    list(TRANSFORM arg_INCLUDE_DIRECTORIES PREPEND "-I")

    set(objects "")
    foreach(source IN LISTS arg_UNPARSED_ARGUMENTS)
        cmake_path(ABSOLUTE_PATH source NORMALIZE)
        cmake_path(GET source FILENAME object)
        set(object "${objects_dir}/${object}.o")
        add_custom_command(OUTPUT "${object}"
            COMMAND "${STASIS_WASI_CLANG}" ${STASIS_C_SCRIPT_FLAGS} "${sysroot}"
                ${arg_INCLUDE_DIRECTORIES} -MD -MF "${object}.d" -c "${source}" -o "${object}"
            DEPENDS "${source}" ${arg_DEPENDS}
            DEPFILE "${object}.d"
            COMMENT "Compiling ${source} for the C script ${name}.wasm"
            VERBATIM
        )
        list(APPEND objects "${object}")
    endforeach()

    add_custom_command(OUTPUT "${script}"
        COMMAND "${STASIS_WASI_CLANG}" ${STASIS_C_SCRIPT_FLAGS} ${STASIS_C_SCRIPT_LINK_FLAGS}
            "${sysroot}" ${objects} -o "${script}"
        DEPENDS ${objects}
        COMMENT "Linking the C script ${name}.wasm"
        VERBATIM
    )
    add_custom_target(stasis_script_${name} ALL DEPENDS "${script}")
endfunction()
