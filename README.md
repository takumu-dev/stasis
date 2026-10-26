# Stasis

Stasis is a game scripting library. It runs scripts compiled to WebAssembly in a sandbox inside a
game, and saves and restores their state as snapshots.

Stasis is at an early stage and has no release yet. So far, its C API declares the status codes its
functions will return, and nothing runs scripts yet.

## Requirements

- x86-64 Linux or Windows. Configuring for any other platform stops with a message.
- A C compiler: GCC 10, Clang 11 or MSVC 14.44, or newer. The library needs no C++ compiler. Stasis's
  own tests use one to check that the public headers compile as C++.
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
| `gcc-debug`, `clang-debug`, `msvc-debug` | Debug, with a static library |
| `gcc-release`, `clang-release`, `msvc-release` | Release, with a shared library |
| `sanitize` | Clang, debug and static, with AddressSanitizer on everything and UndefinedBehaviorSanitizer on Stasis's own code |
| `tsan` | Clang, debug and static, with ThreadSanitizer |
| `msvc-asan` | MSVC, debug and static, with AddressSanitizer |
| `tidy` | Clang, debug, with wasi-sdk's clang-tidy run on Stasis's own code |
| `lint` | Nothing: it checks that the sources are formatted as wasi-sdk's clang-format would format them |

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

## Using Stasis in a host

A host, the game or engine that embeds Stasis, adds it to its own CMake build with
`add_subdirectory` or `FetchContent`, and links `stasis::stasis`:

```cmake
add_subdirectory(stasis)
target_link_libraries(host PRIVATE stasis::stasis)
```

The build never downloads anything. The library is static unless `BUILD_SHARED_LIBS` is on, and a
shared library exports Stasis's API alone. A host's build of Stasis never has warnings turned into
errors, so a newer compiler can't break it.

These options default to on when Stasis is the top-level project and off when it's a subproject:

| Option | What it does |
|---|---|
| `STASIS_BUILD_TESTS` | Builds Stasis's tests |
| `STASIS_BUILD_TOOLS` | Adds the `check-format` and `format` targets, where the pinned wasi-sdk is found |
| `STASIS_INSTALL` | Installs the library and its public header |
