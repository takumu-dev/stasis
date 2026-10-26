# Checks a built Stasis library through the symbols it defines, exports and references, as nm lists
# them on Linux and dumpbin on Windows.
#
#   cmake -DCHECK=<check> -DLIBRARY=<file> -DSHARED=<bool> (-DNM=<nm> | -DDUMPBIN=<dumpbin>)
#         -P check_library.cmake
#
# CHECK is one of:
#
#   holds_wamr      WAMR is compiled into the library. A static library defines WAMR's functions,
#                   and so does a shared one on Linux. A DLL exports the two functions WAMR always
#                   exports on Windows, which only WAMR's code inside it can.
#   exports_only_stasis_api
#                   a shared library exports Stasis's API alone: functions named stasis_*, and on
#                   Windows the two functions WAMR always exports.
#   references_no_signal_or_jump_functions
#                   the library references no sigaction, sigaltstack, setjmp or longjmp, in any of
#                   their forms, so it installs no signal handler and never jumps across a host's
#                   frames.

cmake_minimum_required(VERSION 3.25...4.4)

# The functions WAMR's own code marks for export from a DLL, whatever Stasis asks of it.
set(wamr_dll_exports wasm_runtime_malloc wasm_runtime_free)

# Runs the tool on the library with the arguments that follow `variable`, and sets `variable` to the
# lines it prints.
function(stasis_run_tool variable)
    if(NM)
        set(command "${NM}" ${ARGN} "${LIBRARY}")
    else()
        set(command "${DUMPBIN}" /nologo ${ARGN} "${LIBRARY}")
    endif()
    execute_process(COMMAND ${command}
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        list(JOIN command " " command)
        message(FATAL_ERROR "${command} failed (${result}):\n${error}")
    endif()
    # Symbol names hold no semicolon, so each line becomes an element of a list.
    string(REPLACE "\r" "" output "${output}")
    string(REPLACE "\n" ";" lines "${output}")
    set(${variable} "${lines}" PARENT_SCOPE)
endfunction()

# Sets `variable` to the names of the symbols nm lists with the arguments that follow it, in its
# POSIX format, where each symbol's line starts with its name and its type, and an archive's members
# have lines of their own. ELF symbol versions, as in `longjmp@GLIBC_2.2.5`, are dropped.
function(stasis_nm_symbols variable)
    stasis_run_tool(lines -P ${ARGN})
    set(names "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^([^ @]+)[^ ]* [A-Za-z]( |$)")
            list(APPEND names "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${variable} "${names}" PARENT_SCOPE)
endfunction()

# Sets `variable` to the external symbols that dumpbin /symbols lists for a static library, those
# its members define when `kind` is SECT and those they reference when it's UNDEF.
function(stasis_dumpbin_symbols variable kind)
    stasis_run_tool(lines /symbols)
    list(FILTER lines INCLUDE REGEX " ${kind}[0-9A-F]* .* External +\\| ")
    list(TRANSFORM lines REPLACE "^.*\\| +([^ ]+).*$" "\\1")
    set(${variable} "${lines}" PARENT_SCOPE)
endfunction()

# Sets `variable` to the functions a DLL exports, from dumpbin /exports, whose table gives each
# export's ordinal, hint, address and name.
function(stasis_dll_exports variable)
    stasis_run_tool(lines /exports)
    list(FILTER lines INCLUDE REGEX "^ +[0-9]+ +[0-9A-F]+ +[0-9A-F]+ +[^ ]+$")
    list(TRANSFORM lines REPLACE "^.* " "")
    set(${variable} "${lines}" PARENT_SCOPE)
endfunction()

# Sets `variable` to the functions a DLL imports, from dumpbin /imports, which lists each one
# indented under its DLL, after its hint.
function(stasis_dll_imports variable)
    stasis_run_tool(lines /imports)
    list(FILTER lines INCLUDE REGEX "^ +[0-9A-F]+ +[^ ]+$")
    list(TRANSFORM lines REPLACE "^.* " "")
    set(${variable} "${lines}" PARENT_SCOPE)
endfunction()

# Stops, saying what the check found wrong, with the symbols at fault one to a line.
function(stasis_fail problem)
    list(TRANSFORM ARGN PREPEND "\n  ")
    list(JOIN ARGN "" symbols)
    message(FATAL_ERROR "${LIBRARY} ${problem}${symbols}")
endfunction()

if(NOT NM AND NOT DUMPBIN)
    message(FATAL_ERROR "check_library.cmake needs NM or DUMPBIN to list the library's symbols.")
endif()

if(CHECK STREQUAL "holds_wamr")
    if(SHARED AND DUMPBIN)
        stasis_dll_exports(exports)
        foreach(name IN LISTS wamr_dll_exports)
            if(NOT name IN_LIST exports)
                stasis_fail("doesn't export ${name}, so WAMR isn't inside it.")
            endif()
        endforeach()
    else()
        if(DUMPBIN)
            stasis_dumpbin_symbols(defined SECT)
        else()
            stasis_nm_symbols(defined --defined-only)
        endif()
        if(NOT "wasm_runtime_full_init" IN_LIST defined)
            stasis_fail("doesn't define WAMR's wasm_runtime_full_init, so WAMR isn't inside it.")
        endif()
    endif()

elseif(CHECK STREQUAL "exports_only_stasis_api")
    if(DUMPBIN)
        stasis_dll_exports(exports)
    else()
        stasis_nm_symbols(exports -D --defined-only)
    endif()
    set(unexpected "")
    foreach(name IN LISTS exports)
        if(NOT name MATCHES "^stasis_" AND NOT (DUMPBIN AND name IN_LIST wamr_dll_exports))
            list(APPEND unexpected "${name}")
        endif()
    endforeach()
    if(unexpected)
        stasis_fail("exports more than Stasis's API:" ${unexpected})
    endif()

elseif(CHECK STREQUAL "references_no_signal_or_jump_functions")
    if(SHARED AND DUMPBIN)
        stasis_dll_imports(references)
    elseif(DUMPBIN)
        stasis_dumpbin_symbols(references UNDEF)
    elseif(SHARED)
        stasis_nm_symbols(references -D --undefined-only)
    else()
        stasis_nm_symbols(references --undefined-only)
    endif()
    list(FILTER references INCLUDE REGEX "sigaction|sigaltstack|setjmp|longjmp")
    if(references)
        list(REMOVE_DUPLICATES references)
        stasis_fail("references functions that touch signals or jump across frames:" ${references})
    endif()

else()
    message(FATAL_ERROR "check_library.cmake has no check named \"${CHECK}\".")
endif()
