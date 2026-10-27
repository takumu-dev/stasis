# Installs the tools that pins.json pins, for this machine, each checked against its SHA-256:
#
#   cmake -DPREFIX=<directory> [-DTOOLS=<tool;...>] -P install.cmake
#
# TOOLS names the tools to install, by default every one: wasi-sdk, wasm-tools, llvm, cmake, ninja
# and doxygen. Each goes into a directory of its own, PREFIX/<tool>, with only the files Stasis's
# build uses, and the licences its archive holds. A tool this script installed before stays as it
# is at its pinned version, and is replaced at any other. A directory the script didn't install is
# never touched. Once done, it prints where the build finds each tool.
#
# The script runs apart from the build, which never downloads anything. It installs the tools alike
# for developers and for CI.

cmake_minimum_required(VERSION 3.25...4.4)

if(NOT PREFIX)
    message(FATAL_ERROR
        "Usage: cmake -DPREFIX=<directory> [-DTOOLS=<tool;...>] -P ${CMAKE_CURRENT_LIST_FILE}"
    )
endif()
file(TO_CMAKE_PATH "${PREFIX}" prefix)
cmake_path(ABSOLUTE_PATH prefix NORMALIZE)
string(REGEX REPLACE "(.)/$" "\\1" prefix "${prefix}")

cmake_host_system_information(RESULT host QUERY OS_NAME OS_PLATFORM)
# On Windows, OS_PLATFORM is the PROCESSOR_ARCHITECTURE environment variable, which a 32-bit program
# sets to x86 even on x86-64 Windows, and passes on to the programs it runs, this one included. It
# keeps the real architecture in PROCESSOR_ARCHITEW6432, which CMake's own
# CMAKE_HOST_SYSTEM_PROCESSOR prefers too.
if(CMAKE_HOST_WIN32 AND DEFINED ENV{PROCESSOR_ARCHITEW6432})
    list(GET host 0 system)
    set(host "${system};$ENV{PROCESSOR_ARCHITEW6432}")
endif()
if(host MATCHES "^Linux;(x86_64|AMD64)$")
    set(platform linux-x86_64)
elseif(host MATCHES "^Windows;(x86_64|AMD64)$")
    set(platform windows-x86_64)
else()
    list(JOIN host " on " host)
    message(FATAL_ERROR "The tools are pinned for x86-64 Linux and Windows only; this is ${host}.")
endif()

# Records which files of `tool` to install on `platform`, from its pinned archive, which holds them
# under the directory TOP or, when TOP is empty, at its root. FILES gives each file or directory to
# install, as its path under TOP, and PRUNE gives what to leave out of those directories.
# ENVIRONMENT says how the build finds the tool, for the script to print: through the PATH, or an
# environment variable it names, then the directory to give it, relative to the tool's own.
#
# The paths name the pinned releases' files, so moving a pin means checking them too. A file the new
# archive lacks stops the install.
function(stasis_tool_files tool platform)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "TOP" "FILES;PRUNE;ENVIRONMENT")
    foreach(field IN ITEMS TOP FILES PRUNE ENVIRONMENT)
        set(${tool}_${platform}_${field} "${arg_${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

# wasi-sdk builds C scripts: clang, wasm-ld, llvm-ar and llvm-ranlib, the resource directory with
# the builtins for wasm32-wasip1, and the C library for wasm32-wasip1 with no start files. Its
# clang-format and clang-tidy format and check Stasis's own C. The build reads its version from
# VERSION. The archives hold no licence.
set(wasi_sdk_target
    VERSION
    lib/clang/23/include
    lib/clang/23/lib/wasm32-unknown-wasip1
    share/wasi-sysroot/include/wasm32-wasip1
    share/wasi-sysroot/lib/wasm32-wasip1/libc.a
)
# C++'s headers, with and without exceptions.
set(wasi_sdk_cxx_headers
    share/wasi-sysroot/include/wasm32-wasip1/eh
    share/wasi-sysroot/include/wasm32-wasip1/noeh
)
stasis_tool_files(wasi-sdk linux-x86_64
    TOP wasi-sdk-34.0-x86_64-linux
    FILES
        bin/clang
        bin/clang-23
        bin/wasm-ld
        bin/lld
        bin/llvm-ar
        bin/llvm-ranlib
        bin/clang-format
        bin/clang-tidy
        lib/libLLVM.so.23.1-wasi-sdk
        lib/libclang-cpp.so.23.1-wasi-sdk
        lib/libedit.so.0
        lib/libedit.so.0.0.76
        ${wasi_sdk_target}
    PRUNE ${wasi_sdk_cxx_headers}
    ENVIRONMENT WASI_SDK_PATH .
)
# For Windows, each program links LLVM in.
stasis_tool_files(wasi-sdk windows-x86_64
    TOP wasi-sdk-34.0-x86_64-windows
    FILES
        bin/clang.exe
        bin/wasm-ld.exe
        bin/llvm-ar.exe
        bin/llvm-ranlib.exe
        bin/clang-format.exe
        bin/clang-tidy.exe
        ${wasi_sdk_target}
    PRUNE ${wasi_sdk_cxx_headers}
    ENVIRONMENT WASI_SDK_PATH .
)

# wasm-tools builds WAT modules and validates scripts.
stasis_tool_files(wasm-tools linux-x86_64
    TOP wasm-tools-1.262.0-x86_64-linux
    FILES
        wasm-tools
        LICENSE-APACHE
        LICENSE-Apache-2.0_WITH_LLVM-exception
        LICENSE-MIT
    ENVIRONMENT PATH .
)
stasis_tool_files(wasm-tools windows-x86_64
    TOP wasm-tools-1.262.0-x86_64-windows
    FILES
        wasm-tools.exe
        LICENSE-APACHE
        LICENSE-Apache-2.0_WITH_LLVM-exception
        LICENSE-MIT
    ENVIRONMENT PATH .
)

# LLVM's own release is the newest Clang: clang and clang++, and the resource directory with the
# sanitizer runtimes and libFuzzer. It links with the system's linker. The archives hold no licence.
#
# On Linux, the sanitizers' reports name functions and lines through llvm-symbolizer. The system's
# addr2line, their fallback, can't read the line tables Clang writes.
stasis_tool_files(llvm linux-x86_64
    TOP LLVM-23.1.4-Linux-X64
    FILES
        bin/clang
        bin/clang++
        bin/clang-23
        bin/llvm-symbolizer
        lib/clang/23/include
        lib/clang/23/lib/x86_64-unknown-linux-gnu
        lib/clang/23/share
    ENVIRONMENT PATH bin
)
stasis_tool_files(llvm windows-x86_64
    TOP clang+llvm-23.1.4-x86_64-pc-windows-msvc
    FILES
        bin/clang.exe
        bin/clang++.exe
        lib/clang/23/include
        lib/clang/23/lib/windows
        lib/clang/23/share
    ENVIRONMENT PATH bin
)

# CMake runs the build and the tests. The archives hold its licence and those of the libraries it
# bundles.
set(cmake_licences
    doc/cmake/LICENSE.rst
    doc/cmake/cmcppdap
    doc/cmake/cmcurl
    doc/cmake/cmlibarchive
    doc/cmake/cmliblzma
    doc/cmake/cmlibrhash
    doc/cmake/cmlibuv
    doc/cmake/cmnghttp2
    doc/cmake/cmsys
    doc/cmake/cmzlib
    doc/cmake/cmzstd
)
stasis_tool_files(cmake linux-x86_64
    TOP cmake-4.4.4-linux-x86_64
    FILES bin/cmake bin/ctest share/cmake-4.4 ${cmake_licences}
    ENVIRONMENT PATH bin
)
# Ninja calls cmcldeps for the dependencies of resource files that MSVC compiles.
stasis_tool_files(cmake windows-x86_64
    TOP cmake-4.4.4-windows-x86_64
    FILES bin/cmake.exe bin/ctest.exe bin/cmcldeps.exe share/cmake-4.4 ${cmake_licences}
    ENVIRONMENT PATH bin
)

# The archives hold no licence.
stasis_tool_files(ninja linux-x86_64 FILES ninja ENVIRONMENT PATH .)
stasis_tool_files(ninja windows-x86_64 FILES ninja.exe ENVIRONMENT PATH .)

# Doxygen for Windows reads C through a libclang of its own. Only the archive for Linux holds the
# licence.
stasis_tool_files(doxygen linux-x86_64
    TOP doxygen-1.18.0
    FILES bin/doxygen LICENSE
    ENVIRONMENT PATH bin
)
stasis_tool_files(doxygen windows-x86_64
    FILES doxygen.exe libclang.dll
    ENVIRONMENT PATH .
)

file(READ "${CMAKE_CURRENT_LIST_DIR}/pins.json" pins)
string(JSON count LENGTH "${pins}")
math(EXPR last "${count} - 1")
set(pinned "")
foreach(index RANGE ${last})
    string(JSON tool MEMBER "${pins}" ${index})
    list(APPEND pinned "${tool}")
endforeach()
if(NOT DEFINED TOOLS)
    set(TOOLS ${pinned})
endif()
foreach(tool IN LISTS TOOLS)
    if(NOT tool IN_LIST pinned)
        list(JOIN pinned ", " pinned)
        message(FATAL_ERROR "No tool is pinned as ${tool}. The tools are ${pinned}.")
    elseif(NOT DEFINED ${tool}_${platform}_FILES)
        message(FATAL_ERROR "This script doesn't say which files of ${tool} to install.")
    endif()
endforeach()

# Installs `tool` as PREFIX/<tool>, unless it's there already at its pinned version. The directory
# holds a file, .pin, that names the archive it came from.
function(stasis_install_tool tool)
    string(JSON version GET "${pins}" ${tool} version)
    string(JSON url GET "${pins}" ${tool} ${platform} url)
    string(JSON sha256 GET "${pins}" ${tool} ${platform} sha256)
    set(top "${${tool}_${platform}_TOP}")

    set(destination "${prefix}/${tool}")
    set(pin "${tool} ${version} ${url} ${sha256}\n")
    if(EXISTS "${destination}/.pin")
        file(READ "${destination}/.pin" installed)
        if(installed STREQUAL pin)
            message(STATUS "${tool} ${version} is installed already")
            return()
        endif()
    elseif(EXISTS "${destination}")
        message(FATAL_ERROR
            "${destination} exists, and this script didn't install it. Move it out of the way, or "
            "choose another PREFIX."
        )
    endif()

    # The archive stays in the working directory until the tool is installed, so that a second run
    # after a failure downloads it only if it's incomplete.
    set(work "${prefix}/${tool}.part")
    cmake_path(GET url FILENAME archive)
    message(STATUS "Downloading ${tool} ${version} from ${url}")
    file(DOWNLOAD "${url}" "${work}/${archive}"
        EXPECTED_HASH SHA256=${sha256}
        TLS_VERIFY ON
        STATUS status
    )
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 reason)
        message(FATAL_ERROR "Downloading ${url} failed: ${reason}")
    endif()

    message(STATUS "Installing ${tool} ${version} in ${destination}")
    set(root "${work}/files")
    if(top)
        string(APPEND root "/${top}")
        list(TRANSFORM ${tool}_${platform}_FILES PREPEND "${top}/" OUTPUT_VARIABLE patterns)
    else()
        set(patterns ${${tool}_${platform}_FILES})
    endif()
    file(REMOVE_RECURSE "${work}/files")
    file(ARCHIVE_EXTRACT INPUT "${work}/${archive}" DESTINATION "${work}/files"
        PATTERNS ${patterns}
    )
    foreach(path IN LISTS ${tool}_${platform}_PRUNE)
        file(REMOVE_RECURSE "${root}/${path}")
    endforeach()
    file(WRITE "${root}/.pin" "${pin}")

    file(REMOVE_RECURSE "${destination}")
    file(RENAME "${root}" "${destination}")
    file(REMOVE_RECURSE "${work}")
endfunction()

file(MAKE_DIRECTORY "${prefix}")
set(variables "")
set(path "")
foreach(tool IN LISTS TOOLS)
    stasis_install_tool(${tool})

    list(GET ${tool}_${platform}_ENVIRONMENT 0 name)
    list(GET ${tool}_${platform}_ENVIRONMENT 1 directory)
    if(directory STREQUAL ".")
        set(directory "${prefix}/${tool}")
    else()
        set(directory "${prefix}/${tool}/${directory}")
    endif()
    file(TO_NATIVE_PATH "${directory}" directory)
    if(name STREQUAL "PATH")
        list(APPEND path "${directory}")
    else()
        string(APPEND variables "\n  ${name}=${directory}")
    endif()
endforeach()

# A list on the PATH is separated by ; on Windows and : elsewhere.
if(platform STREQUAL "linux-x86_64")
    list(JOIN path ":" path)
endif()
set(settings "${variables}")
if(path)
    string(APPEND settings "\n  PATH=${path}, before the rest of the PATH")
endif()
message(STATUS "The tools are installed. The build finds them with these settings:${settings}")
