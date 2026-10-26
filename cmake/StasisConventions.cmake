# The compiler settings of everything Stasis compiles itself: the library and its tests.
#
# Everything here is PRIVATE to the target, so none of it reaches a host. Warnings become errors only
# through CMAKE_COMPILE_WARNING_AS_ERROR, which Stasis's own presets set; a host building Stasis
# keeps its own choice.

include_guard(GLOBAL)

# Turns on the strict warnings, for C and for C++, which only the header checks compile.
function(stasis_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /w14062 # an enumerator left out of a switch with no default
            /w14242 # conversion from one integer type to a smaller one
            /w14254 # a larger bit field assigned to a smaller one
            /w14287 # an unsigned and a negative constant compared
            /w14296 # an expression that is always true or false
            /w14311 # a pointer truncated
            /w14545 /w14546 /w14547 /w14549 # a comma operator with no effect
            /w14555 # an expression with no effect
            /w14619 # a pragma naming a warning that doesn't exist
            /w14826 # a sign-extending conversion
            /w14905 /w14906 # a string literal cast to the wrong kind of string
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        )
        # MSVC deprecates standard C functions such as fopen in favour of its own _s variants,
        # which the other C libraries lack.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wcast-align
            -Wcast-qual
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
            -Wundef
            -Wwrite-strings
            $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
            $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
            $<$<COMPILE_LANGUAGE:C>:-Wold-style-definition>
        )
        if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
            # Checks each Doxygen comment against the declaration it documents.
            target_compile_options(${target} PRIVATE -Wdocumentation)
        endif()
    endif()
endfunction()

# Applies the settings of Stasis's own C: ISO C17 with no extensions, the strict warnings, nothing
# visible outside a shared library unless STASIS_API marks it, and UndefinedBehaviorSanitizer when
# STASIS_UBSAN is on.
#
# GCC and Clang reject whatever ISO C17 forbids, and VLAs, which MSVC lacks.
function(stasis_target_conventions target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        C_VISIBILITY_PRESET hidden
    )
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -pedantic-errors -Werror=vla)
    endif()
    stasis_target_warnings(${target})
    if(STASIS_UBSAN)
        target_compile_options(${target} PRIVATE
            -fsanitize=undefined
            -fno-sanitize-recover=undefined
        )
        target_link_options(${target} PRIVATE -fsanitize=undefined)
    endif()
endfunction()
