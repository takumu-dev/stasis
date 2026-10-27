# Stasis

Stasis is a game scripting library. It runs scripts compiled to WebAssembly in a sandbox inside a
game, and saves and restores their state as snapshots.

Stasis is at an early stage and has no release yet. So far, its C API declares the status codes its
functions will return and the call that generated code makes into it, the library holds the
WebAssembly runtime its scripts will run on, and its generator checks interface files and writes
their host side, but nothing runs scripts yet.

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
| wasm-tools | 1.262.0 | The tests build WAT modules with it, validate scripts, and read the generator's WIT renderings. |
| LLVM | 23.1.4 | The newest Clang, to test Stasis with, and llvm-symbolizer, which names functions and lines in the sanitizers' reports. |
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

## Continuous integration

CircleCI builds and tests every push to `develop` and to the `feature/*`, `release/*` and
`hotfix/*` branches. Each job is one workflow preset, run in a Docker image that
`.circleci/config.yml` names by its digest:

| Image | Compilers | Presets |
|---|---|---|
| `ghcr.io/takumu-dev/stasis-ci-floor` | GCC 10.3 and Clang 11.0.1, the floors | `gcc-debug`, `gcc-release`, `clang-debug` and `clang-release` |
| `ghcr.io/takumu-dev/stasis-ci-newest` | GCC 16.2 and Clang 23.1.4, the newest releases | the same four, and `sanitize`, `tsan`, `tidy` and `lint` |

`stasis-ci-floor` is the SDK of Valve's Steam Runtime 3, sniper, as Valve ships it, with the pinned
wasi-sdk and wasm-tools. `stasis-ci-newest` is GCC's official image with every pinned tool, and
its plain `gcc` and `clang` are the newest releases. Both install the tools with
`tools/install.cmake`, and their Dockerfiles are under `ci/`.

A job runs on any machine with Docker, with the image's digest from `.circleci/config.yml`:

```sh
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/src" -w /src \
    ghcr.io/takumu-dev/stasis-ci-newest@sha256:<digest> cmake --workflow --preset sanitize
```

The preset builds into `build/<preset>` as usual, and that tree then belongs to the image: remove it
before building the preset outside the image, and the other way round.

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
| `STASIS_INSTALL` | Installs the library, its public headers and the third-party notices |

`STASIS_WAMR_INTERPRETER` chooses the interpreter scripts run on, `fast` or `classic`, in a host's
build as in Stasis's own.

## Interface files and the generator

A host declares what its scripts may call and what they provide in one interface file, with the
extension `.stasis`:

```rust
/// The dungeon's scripting interface.
interface acme:dungeon@1.2.0;

group world {
    record vec2 { x: f32, y: f32 }

    /// Anything that lives in the dungeon.
    object entity {
        fn name() -> string;
        fn position() -> vec2;
    }

    fn find(name: string) -> entity;
}

group combat {
    use world::{entity};

    fn attack(target: entity, damage: u32) -> bool;
}

script npc {
    uses world, combat;

    fn tick(frame: u64);
    optional fn on_hit(by: entity, damage: u32);
}
```

- The header names the interface and its version, `<namespace>:<name>@<major>.<minor>.<patch>`,
  with no pre-release suffix or build metadata. The namespace `stasis` is reserved.
- A group declares host functions, records, and kinds of host object, which are `object`s with
  methods. Its declarations may come in any order, and `use` brings in another group's records and
  objects, as long as no groups use each other in a cycle. A record holds at least one field, and
  never itself.
- The one kind of script, `script`, names the groups its scripts may call in its one `uses`, and
  declares the entry points they provide, which are required unless they're `optional`.
- The types are `u8` to `u64`, `s8` to `s64`, `f32`, `f64`, `bool`, `string`, records, and
  objects, whose handles are written as the object's name.
- Names are lowercase ASCII snake_case, and each of their words starts with a letter. A name is
  unique within its scope, and can't be a keyword, a built-in type, or one of the words reserved
  for later versions of the format: `list`, `tuple`, `enum`, `flags`, `variant`, `option`,
  `result` and `char`.
- `///` starts a doc comment, which documents the declaration after it, and `//` any other comment.
  The file is UTF-8, with non-ASCII only in comments. Lists may end in a trailing comma.

`stasis-gen`, Stasis's generator, reads an interface file:

| Command | What it does |
|---|---|
| `stasis-gen check <file>` | Checks the interface, and writes nothing |
| `stasis-gen host [--prefix <prefix>] [--output-directory <dir>] <file>` | Writes the host's side of the interface, as a C header and source |
| `stasis-gen signatures <file>` | Prints each host function's, method's and entry point's name, the encoding of its signature, and that encoding's hash, which names it in a script |
| `stasis-gen wit [--output-directory <dir>] <file>` | Writes the interface's WIT rendering |

It reports every error in the file in one run, on stderr, as `<file>:<line>:<column>: error:
<message>`, where columns count bytes. A `note:` line after an error points at another place in
the file that bears on it, such as the first of two declarations with the same name. There are no
warnings. It exits with 0 when the interface has no errors, 1 when it has, and 2 when it can't run,
as for a bad command line or a file it can't read or write. It writes nothing when the interface
has errors, and leaves alone a file that already holds what it would write.

### The host's side

`stasis-gen host` writes a header and a source, which a host compiles into its own build. The
header is C99 that also compiles as C++11 or later, and the source is C99. They hold:

- the interface's descriptors, as one constant `stasis_interface`, which the host loads each script
  against. `stasis_interface.h` declares what they hold, and the C API reads them;
- a prototype of each host function, which the host defines, and a thunk through which Stasis calls
  it;
- a typed caller for each entry point, which returns a `stasis_status` and writes any result
  through its last parameter;
- an opaque type for each kind of host object, whose objects cross as pointers to it, and a struct
  for each record, which crosses by value;
- each kind of host object's id, and each entry point's.

`///` doc comments become Doxygen comments, and a host function's also say what its strings and
handles point at, and on which thread and stack it runs. Every file starts with
`/* Generated by stasis-gen <version> from <file name>. Do not edit. */`, and the same interface
gives the same bytes on every machine.

For `acme:dungeon@1.2.0`, every C name starts with the prefix `acme_dungeon_`, which `--prefix`
replaces, and the files are `acme_dungeon.h` and `acme_dungeon.c`:

```c
/* world.find: a host function, which the host defines */
acme_dungeon_world_entity *acme_dungeon_world_find(stasis_instance *instance, stasis_string name);

/* world.entity.name: a method, which takes the host object it's called on as self */
stasis_string acme_dungeon_world_entity_name(
    stasis_instance *instance, acme_dungeon_world_entity *self);

/* npc.on_hit: an entry point's typed caller */
stasis_status acme_dungeon_npc_on_hit(
    stasis_instance *instance, acme_dungeon_world_entity *by, uint32_t damage);

#define ACME_DUNGEON_WORLD_ENTITY_KIND 0
#define ACME_DUNGEON_NPC_ON_HIT_ENTRY 1
```

Names join their scopes with `_`, so two declarations can come out with the same C name, as
`world.entity_find` would beside `world.entity.find`, or as `world.vec2_free` would beside the
`_free` helper that the C bindings give a record `world.vec2` that holds a string. That's an error
in the interface, which every command reports. A parameter or field named after a keyword of C or
C++, a common lowercase macro such as `errno` or `small`, or one of `instance`, `self` and
`result`, takes a `_` after its name in C. One that comes out as a type or function the host's
side uses, such as `uint32_t` or `stasis_call`, or as another declaration's C name, is an error.

### The WIT rendering

`stasis-gen wit` writes the interface as WIT, for the component model's tools to read, in one file
named after its namespace and name, such as `acme-dungeon.wit` for `acme:dungeon@1.2.0`. The
interface is a WIT package of the same name and version, in which:

- each group is an interface, each record a record, and each kind of host object a resource with
  its methods;
- a handle is `borrow<T>` in a parameter, and `T` in a result or a record's field;
- the kind of script is a world, which imports the groups it uses and exports its required entry
  points;
- each optional entry point is an interface of its own, `<kind>-<entry point>`, which a script's
  world exports if the script provides the entry point.

```wit
interface combat {
    use %world.{entity};

    attack: func(target: borrow<entity>, damage: u32) -> bool;
}

world npc {
    import %world;
    import combat;

    export tick: func(frame: u64);
}

/// The optional entry point `npc.on_hit`, which a script's world exports if the script provides it.
interface npc-on-hit {
    use %world.{entity};

    on-hit: func(by: borrow<entity>, damage: u32);
}
```

Names are in kebab-case, with a `%` before each that's a keyword of WIT's, such as `world`. WIT
compares names without their `-`s, so two names that are the same once their `_`s are dropped, such
as `last_hit` and `lasthit`, are an error in the interface when they're in one scope, or among the
records and objects that a kind of script's entry points take and return. So is a method named as
its kind of host object, and a group named as an optional entry point's interface, as `npc_on_hit`
is beside `npc`'s `on_hit`. Every command reports these errors. The rendering renames two kinds of
name instead, neither of which names anything a component imports or exports: a method's parameter
that WIT takes as `self` becomes `self-param`, beside the handle that WIT calls `self`, and a type
named as an optional entry point is used as `<name>-type` in the entry point's interface.

`///` doc comments stay doc comments. The file starts with the same line as the host's side, which
WIT's tools read as the start of the package's documentation, since they take every comment before
`package` as its doc comment.

### Generating in a build

A build of Stasis that isn't cross-compiling defines the generator as the target
`stasis::generator`. In a host's build, it's built only when something needs it, such as
`stasis_generate`, which adds a command that generates as the host builds, and again whenever the
interface file or the generator changes:

```cmake
stasis_generate(host dungeon.stasis OUTPUT_DIRECTORY gen VARIABLE dungeon_host)
target_sources(game PRIVATE ${dungeon_host})
target_include_directories(game PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)
```

`PREFIX <prefix>` passes `--prefix`, and `stasis_generate(wit ...)` writes the WIT rendering. The
build is configured again whenever the interface file changes, since the files' names come from its
header.

A cross-compiling build can't build the generator. It sets `STASIS_GENERATOR` to the `stasis-gen`
of a native build of the same version of Stasis, which `stasis_generate` checks with `--version`.
Without it, configuring stops at the first `stasis_generate`.
