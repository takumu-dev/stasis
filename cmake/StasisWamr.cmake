# WAMR, the WebAssembly runtime that runs scripts, compiled into Stasis from the unmodified copy of
# its files in third_party/wamr/. WAMR is part of Stasis, never a dependency of the host: Stasis
# picks its features and where its logging and printing go, and nothing of WAMR's own build is used.
#
# Upgrading WAMR is deliberate: copy the new release with tools/vendor_wamr.cmake, then derive the
# file lists in StasisWamrFiles.cmake and the definitions below again, against what WAMR's own
# build-scripts/runtime_lib.cmake would choose.

include_guard(GLOBAL)

include(StasisWamrFiles)

set(STASIS_WAMR_DIR "${PROJECT_SOURCE_DIR}/third_party/wamr")

# WAMR calls host functions through assembly of its own: GNU assembly on Linux and MASM on Windows.
# On Linux it also calls the maths library and threads, which older C libraries keep apart from the
# rest.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    enable_language(ASM_MASM)
else()
    enable_language(ASM)
    find_package(Threads REQUIRED)
endif()

# Adds the object library `target`, which compiles WAMR as Stasis builds it, with the interpreter
# STASIS_WAMR_INTERPRETER names. WAMR's printing goes to the function VPRINTF names, which takes a
# format and a va_list as vprintf does, and its logging to stasis_wamr_log, which discards it.
#
#   stasis_add_wamr(<target> VPRINTF <function>)
#
# WAMR builds as C99 with GNU extensions, as its own build does, since its interpreters use computed
# goto and its Linux layer __thread. It keeps its own lenient warnings, never has them made errors,
# and isn't checked by clang-tidy. None of its settings reach any other target.
function(stasis_add_wamr target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "VPRINTF" "")

    set(sources ${STASIS_WAMR_SOURCES})
    if(STASIS_WAMR_INTERPRETER STREQUAL "classic")
        list(APPEND sources ${STASIS_WAMR_SOURCES_CLASSIC})
        set(fast_interpreter 0)
    else()
        list(APPEND sources ${STASIS_WAMR_SOURCES_FAST})
        set(fast_interpreter 1)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(platform windows)
        list(APPEND sources ${STASIS_WAMR_SOURCES_WINDOWS})
        # WAMR's own build defines COMPILING_WASM_RUNTIME_API with MSVC. Without it, WAMR declares
        # wasm_runtime_malloc and wasm_runtime_free as dllimport, which its definitions of them
        # can't be, and with it as dllexport.
        set(platform_definitions
            BH_PLATFORM_WINDOWS
            COMPILING_WASM_RUNTIME_API=1
            HAVE_STRUCT_TIMESPEC
            _WINSOCK_DEPRECATED_NO_WARNINGS
            WASM_HAVE_MREMAP=0
        )
    else()
        set(platform linux)
        list(APPEND sources ${STASIS_WAMR_SOURCES_LINUX})
        set(platform_definitions
            BH_PLATFORM_LINUX
            _GNU_SOURCE
            WASM_HAVE_MREMAP=1
        )
    endif()
    list(TRANSFORM sources PREPEND "${STASIS_WAMR_DIR}/")

    add_library(${target} OBJECT ${sources})
    target_include_directories(${target} PRIVATE
        "${STASIS_WAMR_DIR}/core/iwasm/interpreter"
        "${STASIS_WAMR_DIR}/core/iwasm/include"
        "${STASIS_WAMR_DIR}/core/shared/platform/${platform}"
        "${STASIS_WAMR_DIR}/core/shared/platform/include"
        "${STASIS_WAMR_DIR}/core/shared/mem-alloc"
        "${STASIS_WAMR_DIR}/core/iwasm/common"
        "${STASIS_WAMR_DIR}/core/shared/utils"
    )

    # Every WASM_* and BH_* setting that WAMR's sources read is defined here, since core/config.h
    # gives defaults of its own that differ from those of WAMR's build: no interpreter and no bulk
    # memory, for two. A 0 or 1 turns a feature off or on.
    set(definitions
        BUILD_TARGET_X86_64
        BH_DEBUG=$<IF:$<CONFIG:Debug>,1,0>
        WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS=1
        # MSVC has no computed goto, so its interpreter loops dispatch through a switch.
        WASM_ENABLE_LABELS_AS_VALUES=$<IF:$<C_COMPILER_ID:MSVC>,0,1>

        # One interpreter, with its full loader. The mini-loader skips validation, and scripts
        # aren't trusted. There's no AOT, since an AOT file handed to the loader would run as
        # native code, and no JIT.
        WASM_ENABLE_INTERP=1
        WASM_ENABLE_FAST_INTERP=${fast_interpreter}
        WASM_ENABLE_MINI_LOADER=0
        WASM_ENABLE_AOT=0
        WASM_ENABLE_AOT_INTRINSICS=0
        WASM_ENABLE_AOT_STACK_FRAME=0
        WASM_ENABLE_AOT_VALIDATOR=0
        WASM_ENABLE_DEBUG_AOT=0
        WASM_ENABLE_DYNAMIC_AOT_DEBUG=0
        WASM_ENABLE_QUICK_AOT_ENTRY=0
        WASM_ENABLE_STATIC_PGO=0
        WASM_ENABLE_WAMR_COMPILER=0
        WASM_ENABLE_JIT=0
        WASM_ENABLE_LAZY_JIT=0
        WASM_ENABLE_FAST_JIT=0
        WASM_ENABLE_FAST_JIT_DUMP=0
        WASM_ENABLE_LINUX_PERF=0
        WASM_ORC_JIT_BACKEND_THREAD_NUM=4
        WASM_ORC_JIT_COMPILE_THREAD_NUM=4

        # The WebAssembly that C scripts built for lime1 use: reference types, for the table index
        # every call_indirect encodes, and bulk memory. No SIMD, which lime1 lacks, no 64-bit or
        # multiple memories, and none of the later proposals.
        WASM_ENABLE_REF_TYPES=1
        WASM_ENABLE_BULK_MEMORY=1
        WASM_ENABLE_SIMD=0
        WASM_ENABLE_SIMDE=0
        WASM_ENABLE_MEMORY64=0
        WASM_ENABLE_MULTI_MEMORY=0
        WASM_ENABLE_EXCE_HANDLING=0
        WASM_ENABLE_TAGS=0
        WASM_ENABLE_EXTENDED_CONST_EXPR=0
        WASM_ENABLE_GC=0
        WASM_ENABLE_STRINGREF=0
        WASM_ENABLE_TAIL_CALL=0
        WASM_CONST_EXPR_STACK_SIZE=4
        WASM_TABLE_MAX_SIZE=1024

        # Trap reports: call stacks, with the names from a script's name section.
        WASM_ENABLE_DUMP_CALL_STACK=1
        WASM_ENABLE_CUSTOM_NAME_SECTION=1
        WASM_ENABLE_COPY_CALL_STACK=0
        WASM_ENABLE_LOAD_CUSTOM_SECTION=0

        # Bounds are checked in software, for linear memory and for the native stack, so WAMR
        # installs no signal or exception handler and touches no host thread's stack. The stack
        # guard is how near the end of the native stack a call may come before WAMR traps: room for
        # its own trap path, and the stack a host function is sure of.
        WASM_DISABLE_HW_BOUND_CHECK=1
        WASM_DISABLE_STACK_HW_BOUND_CHECK=1
        WASM_CONFIGURABLE_BOUNDS_CHECKS=0
        WASM_STACK_GUARD_SIZE=16384
        # Nothing else of the host's process is touched either: the GS register, which the
        # interpreter would set on Linux, Winsock, and the signal that wakes blocking operations.
        WASM_DISABLE_WRITE_GS_BASE=1
        WASM_ENABLE_HOST_SOCKET_INIT=1
        WASM_DISABLE_WAKEUP_BLOCKING_OP=1

        # Memory: WAMR allocates through the host-allocator functions Stasis gives it at
        # initialisation, linear memory included, telling them what each allocation is for. Linear
        # memory is never shrunk, so it's the same on every backend.
        WASM_MEM_ALLOC_WITH_USAGE=1
        WASM_MEM_ALLOC_WITH_USER_DATA=1
        WASM_ENABLE_SHRUNK_MEMORY=0
        WASM_ENABLE_SHARED_HEAP=0
        WASM_ENABLE_GLOBAL_HEAP_POOL=0
        WASM_GLOBAL_HEAP_SIZE=10485760
        WASM_MEM_DUAL_BUS_MIRROR=0
        WASM_GC_MANUALLY=0
        BH_ENABLE_GC_VERIFY=0
        BH_ENABLE_GC_CORRUPTION_CHECK=1
        BH_ENABLE_TRACE_MMAP=0
        BH_MALLOC=wasm_runtime_malloc
        BH_FREE=wasm_runtime_free

        # No threads, shared memory or modules that import other modules.
        WASM_ENABLE_SHARED_MEMORY=0
        WASM_ENABLE_THREAD_MGR=0
        WASM_ENABLE_LIB_PTHREAD=0
        WASM_ENABLE_LIB_PTHREAD_SEMAPHORE=0
        WASM_ENABLE_LIB_WASI_THREADS=0
        WASM_ENABLE_HEAP_AUX_STACK_ALLOCATION=0
        WASM_ENABLE_MULTI_MODULE=0
        WASM_ENABLE_MODULE_INST_CONTEXT=0
        WASM_MAX_INSTANCE_CONTEXTS=8

        # No libraries of WAMR's for scripts: Stasis gives them a system interface of its own.
        WASM_ENABLE_LIBC_BUILTIN=0
        WASM_ENABLE_LIBC_WASI=0
        WASM_ENABLE_UVWASI=0
        WASM_ENABLE_LIBC_EMCC=0
        WASM_ENABLE_LIB_RATS=0
        WASM_ENABLE_WASI_NN=0
        WASM_ENABLE_WASI_NN_GPU=0
        WASM_ENABLE_WASI_NN_EXTERNAL_DELEGATE=0
        WASM_ENABLE_WASI_EPHEMERAL_NN=0
        WASM_ENABLE_SGX_IPFS=0
        WASM_ENABLE_WASM_CACHE=0
        WASM_ENABLE_BASE_LIB=0
        WASM_ENABLE_APP_FRAMEWORK=0

        # No debugging, profiling or testing modes.
        WASM_ENABLE_DEBUG_INTERP=0
        WASM_ENABLE_INSTRUCTION_METERING=0
        WASM_ENABLE_OPCODE_COUNTER=0
        WASM_ENABLE_PERF_PROFILING=0
        WASM_ENABLE_MEMORY_PROFILING=0
        WASM_ENABLE_MEMORY_TRACING=0
        WASM_ENABLE_GC_PERF_PROFILING=0
        WASM_ENABLE_WORD_ALIGN_READ=0
        WASM_ENABLE_SPEC_TEST=0
        WASM_ENABLE_WASI_TEST=0
        WASM_ENABLE_FUZZ_TEST=0

        # Logging goes to Stasis, and printing where VPRINTF says. WAMR's API isn't exported from a
        # shared Stasis.
        WASM_ENABLE_LOG=1
        BH_LOG=stasis_wamr_log
        BH_VPRINTF=${arg_VPRINTF}
        WASM_RUNTIME_API_EXTERN=
        WASM_API_EXTERN=

        ${platform_definitions}
    )
    # The assembly takes no definitions.
    target_compile_definitions(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${definitions}>")

    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_EXTENSIONS ON
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON
        COMPILE_WARNING_AS_ERROR OFF
        C_CLANG_TIDY ""
    )
    # Clang assembles without the preprocessor, so it ignores the dependency flags CMake gives it.
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wno-unused-parameter;-Wno-pedantic>"
            $<$<COMPILE_LANGUAGE:ASM>:-Wa,--noexecstack>
            $<$<COMPILE_LANG_AND_ID:ASM,Clang>:-Wno-unused-command-line-argument>
        )
    endif()
endfunction()

# Compiles WAMR into `target`, one of Stasis's own libraries, from the objects of `wamr`, an object
# library stasis_add_wamr added. The target links the system libraries WAMR calls, and its own
# sources can include WAMR's API, wasm_export.h, as a system header, which the strict warnings of
# Stasis's own code don't apply to.
function(stasis_target_holds_wamr target wamr)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:${wamr}>)
    target_include_directories(${target} SYSTEM PRIVATE "${STASIS_WAMR_DIR}/core/iwasm/include")
    target_compile_definitions(${target} PRIVATE WASM_RUNTIME_API_EXTERN=)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_libraries(${target} PRIVATE ntdll ws2_32)
    else()
        target_link_libraries(${target} PRIVATE Threads::Threads m)
    endif()
endfunction()
