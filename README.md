# Stasis

Stasis is a game scripting library. It runs scripts compiled to WebAssembly in a sandbox inside a
game, and saves and restores their state as snapshots.

Stasis is at an early stage and has no release yet. So far, its C API declares the status codes its
functions will return, and the library holds the WebAssembly runtime its scripts will run on, but
nothing runs scripts yet.

## Requirements

- x86-64 Linux or Windows. Configuring for any other platform stops with a message.
- A C compiler: GCC 10, Clang 11 or MSVC 14.44, or newer, and on Windows MSVC's assembler, MASM.
  The library needs no C++ compiler. Stasis's own tests use one to check that the public headers
  compile as C++.
- CMake 3.25 or newer, and Ninja for the presets.
- For Stasis's own tests and checks, wasi-sdk 34 and wasm-tools 1.262.0, which `tools/install.cmake`
  installs. A host's build of Stasis needs neither.

### Compilers

Stasis's own code is ISO C17, with no compiler extensions. Its public headers are C99 that also
compiles as C++11 or later, and include only `<stddef.h>`, `<stdint.h>` and `<stdbool.h>`, so a
host's compiler reads them in whatever language mode the host builds with.

Each compiler has a floor, the oldest version Stasis supports, which follows a rule:

| Compiler | Floor | Where the floor comes from |
|---|---|---|
| GCC, on Linux | 10 | The default `gcc` of the oldest Steam Runtime SDK that Valve offers for native Linux games, leaving out the legacy scout. That's sniper, with GCC 10.3. The floor becomes steamrt4's GCC 14 once Valve stops offering sniper. |
| Clang, on Linux | 11 | The same rule. Sniper has Clang 11.0.1, and steamrt4 has Clang 19. |
| MSVC, on Windows | 14.44 (Visual Studio 2022 17.14) | The final toolset of the previous Visual Studio major version. The floor moves once the Visual Studio after Visual Studio 2026 ships. |

A floor rises only when its rule moves it, or for a reason given where the change is made, and only
in a minor or major release whose notes name the rise. Raising a floor isn't a breaking change. A
floor is never lowered unless CI tests the new one.

Stasis is tested with each compiler at its floor and at its newest release. Versions in between are
supported but not tested. Other compilers, such as clang-cl and MinGW-w64's GCC, are untested, but
nothing in Stasis should rule them out: fixes for them are welcome, though not promised.

## Tools

Every tool that builds or checks Stasis, apart from the compilers, is pinned in `tools/pins.json`:
its version, and the URL and SHA-256 of its release for x86-64 Linux and for Windows.

| Tool | Version | What it's for |
|---|---|---|
| wasi-sdk | 34 | The tests build C scripts with its clang. Its clang-format and clang-tidy format and check Stasis's own code. |
| wasm-tools | 1.262.0 | The tests build WAT modules with it, and validate scripts. |
| LLVM | 23.1.4 | The newest Clang, to test Stasis with. |
| CMake | 4.4.4 | The newest CMake, to test Stasis with. |
| Ninja | 1.13.2 | The presets. |
| Doxygen | 1.18.0 | The API documentation. |

`tools/install.cmake` downloads them, checks each download against its SHA-256, and installs only
the parts the build uses. Each tool goes into a directory of its own under the prefix:

```sh
cmake -DPREFIX=$HOME/.local/opt/stasis-tools -P tools/install.cmake
```

`-DTOOLS="wasi-sdk;wasm-tools"` installs only those two, which are all the tests need. The script
ends by printing where the build finds each tool: wasi-sdk through the `WASI_SDK_PATH` environment
variable, and the rest on the `PATH`. Run again, it replaces only the tools whose pins have moved.
It runs apart from the build, which never downloads anything.

## Building

Every configuration Stasis builds itself is a preset in `CMakePresets.json`, and a workflow preset
configures, builds and tests in one step:

```sh
cmake --workflow --preset gcc-debug
```

| Preset | What it builds |
|---|---|
| `gcc-debug`, `clang-debug`, `msvc-debug` | Debug, with a static library and the classic interpreter |
| `gcc-release`, `clang-release`, `msvc-release` | Release, with a shared library and the fast interpreter |
| `sanitize` | Clang, debug, static and fast, with AddressSanitizer on everything and UndefinedBehaviorSanitizer on Stasis's own code |
| `tsan` | Clang, debug, static and fast, with ThreadSanitizer |
| `msvc-asan` | MSVC, debug, static and classic, with AddressSanitizer |
| `tidy` | Clang, debug, with wasi-sdk's clang-tidy run on Stasis's own code |
| `lint` | Nothing: it checks that the sources are formatted as wasi-sdk's clang-format would format them, and its configuring runs the licence check |

The presets use the compilers on the `PATH`: `gcc` and `g++`, `clang` and `clang++`, or `cl`, from a
developer command prompt for MSVC. Each builds into `build/<preset>`. Warnings are errors in every
preset. Settings for one machine, such as a compiler under another name, belong in a
`CMakeUserPresets.json`, which git ignores.

Each test case is a CTest test of its own, named after its program and its case, such as
`sha256.nist_monte_carlo`. To run some of them, use for example
`ctest --preset gcc-debug -R '^sha256\.'`. The `format` target reformats every source in place.

The tests build the scripts they run from source, so configuring them finds the pinned tools, and
stops if either is missing or at another version:

- wasi-sdk, in `STASIS_WASI_SDK_PATH`, which defaults to the `WASI_SDK_PATH` environment variable;
- wasm-tools, on the `PATH`, or in `STASIS_WASM_TOOLS`.

The `format` and `check-format` targets and the `tidy` preset use wasi-sdk's clang-format and
clang-tidy too, never others, since each release of them formats and checks a little differently.

## WAMR and other third-party code

Scripts run on [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime), the WebAssembly
Micro Runtime, which is compiled into Stasis's own library. WAMR is part of Stasis, not a dependency
of the host: Stasis chooses its features, sends its logging and printing to functions of its own,
which discard them, and builds it to leave the host's process alone. WAMR checks bounds in
software, so it installs no signal or exception handler and leaves the host's thread stacks alone.
The tests check that the library references none of `sigaction`, `sigaltstack`, `setjmp` and
`longjmp`.

`STASIS_WAMR_INTERPRETER` chooses the interpreter: `fast`, the default, or `classic`, whose trap
reports give offsets in a script's module that map to its source lines.

Everything the build compiles from elsewhere lives under `third_party/<name>/`, unmodified and
pinned to a release, and `third_party/dependencies.json` lists each dependency's version, upstream
commit, SPDX licence and whether it ships, meaning it's compiled into the library.
`third_party/wamr/` holds only the files of WAMR 2.4.5 that the build uses, and its licences.
`tools/vendor_wamr.cmake` copies them from a git checkout of WAMR's release, and
`cmake/StasisWamrFiles.cmake` lists them:

```sh
git clone --depth 1 --branch WAMR-2.4.5 https://github.com/bytecodealliance/wasm-micro-runtime.git ../wamr
cmake -DSOURCE=../wamr -P tools/vendor_wamr.cmake
```

Upgrading WAMR is deliberate. Update its entry in `third_party/dependencies.json`, run the script on
a checkout of the new release, and derive the file lists and the definitions in
`cmake/StasisWamr.cmake` again, against what WAMR's own `build-scripts/runtime_lib.cmake` would
choose.

Configuring Stasis as the top-level project runs the licence check, `cmake/check_licenses.cmake`.
It fails unless:

- each dependency's licence is one that `third_party/licenses.json` allows: Zlib, MIT, BSD-2-Clause,
  BSD-3-Clause, BSL-1.0, ISC, Apache-2.0 with or without the LLVM exception, CC0-1.0 or Unlicense,
  or one an override gives for the version it reviewed;
- each directory under `third_party/` is a dependency the manifest names;
- `THIRD_PARTY_NOTICES.md` lists each dependency that ships, with its licence files. Whatever ships
  Stasis's library ships those dependencies too, and carries this file with it. To write it again,
  run `cmake -DWRITE_NOTICES=THIRD_PARTY_NOTICES.md -P cmake/check_licenses.cmake`.

## Using Stasis in a host

A host, the game or engine that embeds Stasis, adds it to its own CMake build with
`add_subdirectory` or `FetchContent`, and links `stasis::stasis`:

```cmake
add_subdirectory(stasis)
target_link_libraries(host PRIVATE stasis::stasis)
```

The build never downloads anything. The library is static unless `BUILD_SHARED_LIBS` is on, with
WAMR inside it either way. A shared library exports Stasis's API alone, apart from
`wasm_runtime_malloc` and `wasm_runtime_free`, which WAMR always exports from a DLL on Windows. A
static library carries WAMR's own global symbols, some with generic names such as `read_leb`, so a
host that links another copy of WAMR beside it should link Stasis shared. A host's build of Stasis
never has warnings turned into errors, so a newer compiler can't break it.

Whatever ships Stasis ships WAMR, whose licence asks for its notices to go with it: they're in
`THIRD_PARTY_NOTICES.md`, which is installed with the library.

These options default to on when Stasis is the top-level project and off when it's a subproject:

| Option | What it does |
|---|---|
| `STASIS_BUILD_TESTS` | Builds Stasis's tests |
| `STASIS_BUILD_TOOLS` | Adds the `check-format` and `format` targets, where the pinned wasi-sdk is found |
| `STASIS_INSTALL` | Installs the library, its public header and the third-party notices |

`STASIS_WAMR_INTERPRETER` chooses the interpreter scripts run on, `fast` or `classic`, in a host's
build as in Stasis's own.
