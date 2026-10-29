# Stasis

Stasis is a game scripting library. It runs scripts compiled to WebAssembly in a sandbox inside a
game, and saves and restores their state as snapshots.

Stasis is at an early stage and has no release yet. So far, a host can load a script against its
interface, learn whether it's snapshottable, create and start instances of it, call their entry
points and have scripts call its host functions, with scalars, strings, records and handles as
arguments and results, revoke handles, and read the trap report of an instance that traps. The
generator checks interface files and writes their host side and their WIT rendering. The system
interface and snapshots are still to come.

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
`hotfix/*` branches. Each job is one workflow preset. On Linux, it runs in a Docker image that
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

On Windows, the jobs run on CircleCI's own image, `windows-server-2025-gui:2026.10.1`, which has
both Visual Studios:

| Visual Studio | MSVC | Presets |
|---|---|---|
| Visual Studio 2022 Build Tools 17.14 | 14.44, the floor | `msvc-debug` and `msvc-release` |
| Visual Studio 2026 Build Tools 18.11 | 14.51, the newest release | the same two, and `msvc-asan` |

`ci/msvc-shell.ps1` runs each job's preset in the developer shell of its Visual Studio, with that
Visual Studio's CMake and Ninja, once it has checked that cl, CMake and Ninja are at the versions
`.circleci/config.yml` gives. The job installs wasi-sdk and wasm-tools with `tools/install.cmake`.
Windows minutes cost the most, so the Windows jobs start only once every Linux job has passed, and
on a `feature/*` branch only once someone approves them in the workflow too. On a developer's
machine, the same preset runs from a developer command prompt for the same MSVC.

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
choose. Then check what Stasis reads of WAMR beyond its API against the new release's sources: the
frames of a trap, which `src/wamr_frames.c` reads from WAMR's own structures, and the descriptions
of traps that `src/trap.c` maps to kinds, which the trap tests pin.

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

## Running scripts

A host creates a library, loads each script into it against an interface's generated data, and
creates and starts instances of the script, whose entry points it then calls through the typed
callers:

```c
stasis_library *library = NULL;
stasis_library_create(NULL, &library);

char message[1024];
stasis_script *script = NULL;
if (stasis_script_load(library, &acme_dungeon_interface, bytes, size, message, sizeof message,
                       &script) != STASIS_OK) {
    /* message names each problem on a line of its own */
}

stasis_instance *instance = NULL;
stasis_instance_create(script, entity, &instance);
stasis_instance_start(instance);
acme_dungeon_npc_tick(instance, frame);

stasis_instance_destroy(instance);
stasis_script_destroy(script);
stasis_library_destroy(library);
```

- Every function that can fail returns a `stasis_status`, and out-parameters are null when it
  fails. `stasis_status_text` describes each code.
- A library's settings hold the size of the stack each instance keeps its WebAssembly frames on,
  64 KiB by default. A process may hold several libraries, and one library may hold scripts of
  several interfaces.
- Loading copies the module's bytes, so the host may free them as soon as it returns. It checks
  the module in stages, and stops at the first that fails: it must be valid WebAssembly
  (`STASIS_INVALID_MODULE`), built for the interface (`STASIS_WRONG_INTERFACE`), and for its line
  (`STASIS_INCOMPATIBLE_VERSION`), which is the major version, or under 0.x the major and minor
  versions. Then it lists every problem it finds: a required entry point missing, an entry point
  with another signature, or a host function that the interface lacks or declares with another
  signature (`STASIS_INTERFACE_MISMATCH`), and names that don't parse or disagree, a function of
  another type than its signature makes it, or a broken rule (`STASIS_INVALID_SCRIPT`). A script
  built against an older or newer minor version loads, and an entry point that the host's interface
  lacks is ignored.
- Stasis's rules make sure that creating an instance runs no script code and fails only for lack
  of memory. A script has no start section, and exports none of `_initialize`, `__wasm_call_ctors`
  and `__post_instantiate`, which a backend would run by itself. It imports only functions, from
  its interface's groups and `wasi_snapshot_preview1`. Its active data and element segments fit
  its memory and its tables as they start, and its memory starts with at most 65,535 pages.
- A loaded script answers its identity, the SHA-256 of its module's sections without the custom
  ones, so a copy stripped of its debug information is the same script, and which optional entry
  points it provides.
- It also answers whether it's snapshottable, and if not, why, one problem a line. A snapshottable
  script defines and exports one memory that isn't shared, exports every mutable global it defines,
  each a number, and contains none of `table.set`, `table.grow`, `table.fill`, `table.copy`,
  `table.init`, `elem.drop` and `data.drop`. A script that isn't snapshottable still loads and
  runs:

  ```c
  const char *reason = NULL;
  if (!stasis_script_snapshottable(script, &reason)) {
      /* reason says why, such as "global 3 is mutable but not exported" */
  }
  ```
- Creating an instance runs no script code. Starting runs the script's `stasis_initialize` once, if
  it exports one, and only a started instance can be called.
- Starting an instance or calling it may trap, which returns `STASIS_TRAP` and ends the instance
  for good: every later start or call is refused with `STASIS_REFUSED_TRAPPED`, and the host
  recovers by creating another. Until it's destroyed, the instance keeps its trap report: the kind
  of trap, whatever the backend, the backend's message, and the call stack, innermost frame first.
  Each frame names its function by its index, and by its name if the module's name section gives
  one. With the classic interpreter, each frame also has the offset in the module of the
  instruction it was at: the one that trapped, or the call to the frame inside it. With the fast
  interpreter, that's `STASIS_NO_OFFSET`.

  ```c
  if (acme_dungeon_npc_tick(instance, frame) == STASIS_TRAP) {
      const stasis_trap *trap = stasis_instance_trap(instance);
      /* trap->kind is STASIS_TRAP_KIND_UNREACHABLE when a C script calls abort(), and
       * trap->frames holds the functions that were running, abort itself first */
  }
  ```
- Objects are destroyed children first: destroying a script that has instances, or a library that
  has scripts, is refused with `STASIS_REFUSED_IN_USE`. Destroying null does nothing.

### Host functions

A script calls the host functions its interface declares, which the host defines, and which Stasis
calls through the thunks in the host's side. Stasis lifts each argument from the script as the
canonical ABI does it, and lowers the result back. Arguments that flatten into more than 16 core
parameters spill into the script's memory, which Stasis reads them from, and a script that passes
them outside its memory, or misaligned, traps as `STASIS_TRAP_KIND_BOUNDARY_VIOLATION`.

- A host function ends the call it's part of with `stasis_abort`. It then returns as it would
  otherwise, Stasis ignores its result, and the call traps as `STASIS_TRAP_KIND_HOST_ABORT`, with
  the first message it was given. The report's innermost frame is the host function's.

  ```c
  acme_dungeon_world_entity *acme_dungeon_world_find(stasis_instance *instance,
                                                     stasis_string name) {
      acme_dungeon_world_entity *found = world_find(name);
      if (found == NULL) {
          stasis_abort(instance, "no entity has that name");
          return NULL;
      }
      return found;
  }
  ```
- Calls into scripts never nest. While an instance is running on a thread, as it is in each of its
  host functions, starting or calling any instance on that thread, the running one included, is
  refused with `STASIS_REFUSED_NESTED`, and aborting one that isn't running on it with
  `STASIS_REFUSED_NOT_RUNNING`. A host function may still create, destroy and load anything that
  runs no script code: any instance but the running one, whose destruction is refused with
  `STASIS_REFUSED_RUNNING`. A script that wants another to act goes through the host, which calls
  it once the current call has returned.
- A host function runs on the thread and stack that called into its instance, with at least 16 KiB
  of the stack free. A call into a script needs 20 KiB free beyond what the host has used when it
  calls, which covers that and Stasis's own frames, or 52 KiB in a build with AddressSanitizer or
  ThreadSanitizer. A call that comes nearer the end of the native stack, as it enters the script or
  before a host function, traps as `STASIS_TRAP_KIND_STACK_EXHAUSTED`. Stasis installs no signal
  handler and touches no stack, so a host function that uses more stack than it's sure of crashes,
  as any C function would.

### Strings

A string crosses as the canonical ABI lays it out, as UTF-8, with its address and size in the
script's memory. On the host's side it's a `stasis_string`, which isn't NUL-terminated. Whenever
Stasis puts a string into the script's memory, it allocates it there with the script's
`cabi_realloc`, as wit-bindgen's bindings expect, and from then on it's the script's.

- A host function's string argument points into the script's memory, and is valid only until the
  host function returns. Before it runs, Stasis checks that each is in the script's memory and is
  UTF-8. A script that passes one that isn't traps as `STASIS_TRAP_KIND_BOUNDARY_VIOLATION`, with a
  message that names the host function and the argument by its position.
- A host function's string result points at storage that outlives the call. Stasis puts a copy
  into the script's memory, and writes where it is into the return area that the script passed. A
  string whose data is null though it isn't empty, that's larger than 2^31 - 1 bytes, or that isn't
  UTF-8 traps as `STASIS_TRAP_KIND_HOST_ABORT`.

  ```c
  stasis_string acme_dungeon_world_entity_name(stasis_instance *instance,
                                               acme_dungeon_world_entity *self) {
      /* the entity's own name, which outlives the call */
      stasis_string name = {self->name, strlen(self->name)};
      return name;
  }
  ```
- An entry point's string argument is put into the script's memory the same way. One that can't
  cross, as above, makes the call `STASIS_INVALID_ARGUMENT`, before the script runs.
- An entry point's string result is checked as a host function's arguments are. Stasis copies it
  into a buffer that the instance owns, and then calls the entry point's `cabi_post_`, if the
  script exports one, which frees the script's own. So the host's copy stays valid until the next
  call into the instance or its destruction, and after the first few calls, no call allocates.
- A script that takes strings from Stasis, as the results of host functions it imports or the
  arguments of entry points it provides, has to export `cabi_realloc`, or it fails to load with
  `STASIS_INVALID_SCRIPT`, as it does if its `cabi_realloc` or an entry point's `cabi_post_` isn't
  of the canonical ABI's type.
- A `cabi_realloc` that returns null when asked for any bytes traps as
  `STASIS_TRAP_KIND_ALLOCATION_FAILED`, and one that returns an address outside memory as
  `STASIS_TRAP_KIND_BOUNDARY_VIOLATION`. While Stasis is inside
  `cabi_realloc` or a `cabi_post_`, the script may call no import, and one that does traps as
  `STASIS_TRAP_KIND_BOUNDARY_VIOLATION`, so a script can't recurse through the host. When one of
  them traps, the call into the instance traps with that trap's kind, message and frames.
- `cabi_realloc` can grow the script's memory, which can move it, so Stasis takes where the memory
  is afresh after each call to it.

### Records

A record crosses as the canonical ABI lays it out: flattened into the values it holds, in order,
those of the records nested in it included, and in the script's memory as its fields, in order,
each aligned as its type needs. A record is as aligned as its strictest field, and padded to a
multiple of that. On the host's side it's the struct that the generator writes, passed and returned
by value.

```c
acme_dungeon_world_vec2 acme_dungeon_world_entity_position(stasis_instance *instance,
                                                           acme_dungeon_world_entity *self) {
    acme_dungeon_world_vec2 position = {self->x, self->y};
    return position;
}
```

- A function's values flatten into at most 16 core parameters. Past that, an entry point's
  arguments spill into the script's memory, as a host function's do: Stasis allocates a tuple of
  them with the script's `cabi_realloc` and lays them out in it, and then puts each string they hold
  into memory, in order. A script that provides an entry point whose arguments spill has to export
  `cabi_realloc`, even when they hold no string.
- A result that flattens into more than one core value goes through a return area. A script that
  calls a host function passes the address of an area of its own after the arguments, which Stasis
  checks before the host function runs, and an entry point returns the address of one it owns. A
  record that flattens into one core value, such as one that holds a single `f64`, crosses as that
  value.
- A string in a record follows the rules for strings above. In a host function's argument, it
  points into the script's memory until the host function returns. In a host function's result,
  every string is checked before any of them goes into the script's memory. In an entry point's
  result, every string is copied into the instance's buffer before its `cabi_post_` runs.
- Spilled arguments and a return area that are outside the script's memory, or misaligned, trap as
  `STASIS_TRAP_KIND_BOUNDARY_VIOLATION`. A message names a string in a record by the fields it's in,
  outermost first and joined by dots, as in "the field `weapon` of argument 1 of `combat.alarm`".

### Handles

A host object reaches a script only as a handle: a `u32` that names it in one instance. On the
host's side, a host object is a pointer to its kind's opaque type, which Stasis never dereferences,
and which the host converts to and from a pointer to its own object. A host object is that pointer
together with its kind, so one pointer of two kinds is two host objects. Null is never a host
object.

- Each instance has handles of its own. They're issued in order from 1, one for each host object,
  as each first crosses into the instance, so within an instance a host object always crosses as the
  same handle, and a script can compare handles and keep them as keys. 0 is never a handle.
- Handles cross as host functions' arguments and results, as the host object a method is called on,
  as entry points' arguments and results, and as records' fields. A handle crosses as one `i32`, and
  lies in memory as 4 bytes.
- A host function that returns a null host object, in a record or not, traps as
  `STASIS_TRAP_KIND_HOST_ABORT`, with a message that names it, and an entry point's argument that's
  null, or holds one, makes the call `STASIS_INVALID_ARGUMENT`, before the script runs.
- The host revokes an instance's handle for a host object, such as an entity that has died, with
  `stasis_instance_revoke`. It takes the instance, the kind's id and the host object, and does
  nothing if the instance has no handle for it, or the host object is null, as a revoked handle
  reaches a host function. Revoking runs no script code, so it's allowed in
  every state, running included, and a host function may revoke a handle in the instance that
  called it at once. A host that frees a host object without revoking it in every instance that may
  hold its handle leaves those handles naming a dangling pointer, which host functions then
  receive.

  ```c
  void acme_dungeon_combat_kill(stasis_instance *instance, acme_dungeon_world_entity *target) {
      stasis_instance_revoke(instance, ACME_DUNGEON_WORLD_ENTITY_KIND, target);
      world_destroy(target);
  }
  ```
- A revoked handle names nothing, but it's never reused. A script may still pass it to host
  functions, which receive it as null and decide for themselves what a dead entity's query returns,
  and an entry point that returns it gives the host null. A host object whose handle was revoked
  gets a new handle if it crosses into the instance again.
- A handle that the instance was never given is forged. A script that passes a forged handle to a
  host function, or one that names a host object of another kind than the value it crosses as, or
  returns either from an entry point, traps as `STASIS_TRAP_KIND_BOUNDARY_VIOLATION`, before the
  host sees it.
- An instance can be given 4,294,967,295 handles over its life, every `u32` but 0, since handles are
  never reused. A call that needs one more, for a host function's result or an entry point's
  argument, traps as `STASIS_TRAP_KIND_HANDLES_EXHAUSTED`. A host function's call with no memory to
  keep the handles of its result traps as `STASIS_TRAP_KIND_ALLOCATION_FAILED` before the host
  function runs, and an entry point's call that can't make room for the handles of its arguments is
  `STASIS_OUT_OF_MEMORY` before the script runs.
