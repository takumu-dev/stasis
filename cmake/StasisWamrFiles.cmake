# The files of WAMR's that Stasis keeps in third_party/wamr/, as paths relative to it: the sources
# it compiles on any platform and with either interpreter, the headers they include, and the
# licences. tools/vendor_wamr.cmake copies exactly these from WAMR's source tree, and
# StasisWamr.cmake compiles the sources.
#
# The sources are those that WAMR's own build-scripts/runtime_lib.cmake picks for the features
# StasisWamr.cmake sets, on x86-64 Linux and Windows. Its Windows list also has win_atomic.cpp,
# which is empty while shared memory is off, and Stasis leaves it out so that the library needs no
# C++ compiler.

# On every platform, with either interpreter.
set(STASIS_WAMR_SOURCES
    core/iwasm/common/wasm_application.c
    core/iwasm/common/wasm_blocking_op.c
    core/iwasm/common/wasm_c_api.c
    core/iwasm/common/wasm_exec_env.c
    core/iwasm/common/wasm_loader_common.c
    core/iwasm/common/wasm_memory.c
    core/iwasm/common/wasm_native.c
    core/iwasm/common/wasm_runtime_common.c
    core/iwasm/common/wasm_shared_memory.c
    core/iwasm/interpreter/wasm_loader.c
    core/iwasm/interpreter/wasm_runtime.c
    core/shared/mem-alloc/ems/ems_alloc.c
    core/shared/mem-alloc/ems/ems_gc.c
    core/shared/mem-alloc/ems/ems_hmu.c
    core/shared/mem-alloc/ems/ems_kfc.c
    core/shared/mem-alloc/mem_alloc.c
    core/shared/utils/bh_assert.c
    core/shared/utils/bh_bitmap.c
    core/shared/utils/bh_common.c
    core/shared/utils/bh_hashmap.c
    core/shared/utils/bh_leb128.c
    core/shared/utils/bh_list.c
    core/shared/utils/bh_log.c
    core/shared/utils/bh_queue.c
    core/shared/utils/bh_vector.c
    core/shared/utils/runtime_timer.c
)

# Each interpreter.
set(STASIS_WAMR_SOURCES_CLASSIC core/iwasm/interpreter/wasm_interp_classic.c)
set(STASIS_WAMR_SOURCES_FAST core/iwasm/interpreter/wasm_interp_fast.c)

# Each platform, with the assembly that calls host functions.
set(STASIS_WAMR_SOURCES_LINUX
    core/iwasm/common/arch/invokeNative_em64.s
    core/shared/platform/common/posix/posix_blocking_op.c
    core/shared/platform/common/posix/posix_malloc.c
    core/shared/platform/common/posix/posix_memmap.c
    core/shared/platform/common/posix/posix_sleep.c
    core/shared/platform/common/posix/posix_thread.c
    core/shared/platform/common/posix/posix_time.c
    core/shared/platform/linux/platform_init.c
)
set(STASIS_WAMR_SOURCES_WINDOWS
    core/iwasm/common/arch/invokeNative_em64.asm
    core/shared/platform/common/memory/mremap.c
    core/shared/platform/windows/platform_init.c
    core/shared/platform/windows/win_clock.c
    core/shared/platform/windows/win_malloc.c
    core/shared/platform/windows/win_memmap.c
    core/shared/platform/windows/win_socket.c
    core/shared/platform/windows/win_thread.c
    core/shared/platform/windows/win_time.c
    core/shared/platform/windows/win_util.c
)

# The headers the sources include, on either platform.
set(STASIS_WAMR_HEADERS
    core/config.h
    core/iwasm/aot/aot_runtime.h
    core/iwasm/common/wasm_c_api_internal.h
    core/iwasm/common/wasm_exec_env.h
    core/iwasm/common/wasm_loader_common.h
    core/iwasm/common/wasm_memory.h
    core/iwasm/common/wasm_native.h
    core/iwasm/common/wasm_runtime_common.h
    core/iwasm/common/wasm_shared_memory.h
    core/iwasm/common/wasm_suspend_flags.h
    core/iwasm/compilation/aot.h
    core/iwasm/include/lib_export.h
    core/iwasm/include/wasm_c_api.h
    core/iwasm/include/wasm_export.h
    core/iwasm/interpreter/wasm.h
    core/iwasm/interpreter/wasm_interp.h
    core/iwasm/interpreter/wasm_loader.h
    core/iwasm/interpreter/wasm_opcode.h
    core/iwasm/interpreter/wasm_runtime.h
    core/shared/mem-alloc/ems/ems_gc.h
    core/shared/mem-alloc/ems/ems_gc_internal.h
    core/shared/mem-alloc/mem_alloc.h
    core/shared/platform/include/platform_api_extension.h
    core/shared/platform/include/platform_api_vmcore.h
    core/shared/platform/include/platform_common.h
    core/shared/platform/include/platform_wasi_types.h
    core/shared/platform/linux/platform_internal.h
    core/shared/platform/windows/platform_internal.h
    core/shared/platform/windows/win_util.h
    core/shared/utils/bh_assert.h
    core/shared/utils/bh_atomic.h
    core/shared/utils/bh_bitmap.h
    core/shared/utils/bh_common.h
    core/shared/utils/bh_hashmap.h
    core/shared/utils/bh_leb128.h
    core/shared/utils/bh_list.h
    core/shared/utils/bh_log.h
    core/shared/utils/bh_platform.h
    core/shared/utils/bh_queue.h
    core/shared/utils/bh_vector.h
    core/shared/utils/gnuc.h
    core/shared/utils/runtime_timer.h
    core/version.h
)

# WAMR's licence, the licence of wasm_c_api.h, which comes from wasm-c-api, and the attributions
# that say so.
set(STASIS_WAMR_LICENSES
    ATTRIBUTIONS.md
    LICENSE
    samples/wasm-c-api/src/LICENSE
)
