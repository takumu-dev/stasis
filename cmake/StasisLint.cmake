# The checks that build nothing. Each target exists only where its tool is found, so a preset that
# builds it fails at once, naming the target, on a machine that lacks the tool.
#
#   check-format  fails if any source differs from what clang-format would make of it
#   format        reformats every source in place
#
# clang-format is the pinned wasi-sdk's, since each release of clang-format formats a little
# differently.

include_guard(GLOBAL)

include(StasisWasiSdk)

stasis_wasi_sdk_program(STASIS_CLANG_FORMAT clang-format)
if(STASIS_CLANG_FORMAT)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/include/*.h"
        "${PROJECT_SOURCE_DIR}/src/*.c"
        "${PROJECT_SOURCE_DIR}/src/*.h"
        "${PROJECT_SOURCE_DIR}/tests/*.c"
        "${PROJECT_SOURCE_DIR}/tests/*.h"
    )
    add_custom_target(check-format
        COMMAND "${STASIS_CLANG_FORMAT}" --dry-run --Werror ${sources}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Checking the formatting"
        VERBATIM
    )
    add_custom_target(format
        COMMAND "${STASIS_CLANG_FORMAT}" -i ${sources}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Formatting the sources"
        VERBATIM
    )
endif()
