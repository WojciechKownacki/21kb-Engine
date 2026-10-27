# Contributing to 21kb Engine

This is a short, practical guide: how to build, which tests to run, how to report a bug so it can be
reproduced, and how to add a provider module (plugin). Every command below exists in this repository.
For security problems use [SECURITY.md](SECURITY.md), not a public issue.

## Layout

| Path | What is in it |
|---|---|
| `sources/engine` | `kb_engine`: ECS, scenes, assets, scripting, module loader |
| `sources/renderer` | `kb_renderer`: bgfx renderer, materials, post processing |
| `sources/editor`, `sources/hub` | Win32 editor and project hub |
| `sources/game` | `kb_game` player host and `kb_cooker` |
| `sources/tools` | `kb_cli`, the headless command line (`kb_cli help`) |
| `sources/plugins/*` | provider modules: physics, audio, particles, lighting, AI, terrain, World 2D |
| `samples/`, `tests/`, `fuzz/`, `benchmarks/` | samples, the API baseline, fuzz harnesses, benchmarks |
| `scripts/` | packaging, localization, notices; Python tests in `scripts/tests` |
| `docs/` | topic documents; [docs/library_support_matrix.md](docs/library_support_matrix.md) lists what CI builds and tests |

## Build

Requirements: CMake 3.27 or newer and a C++20 compiler. The configure step downloads Lua, Zstandard,
Monocypher and Recast (pinned in the root `CMakeLists.txt`), so it needs network access. The editor and the
renderer build on Windows only (`KB_BUILD_EDITOR`, `KB_BUILD_RENDERER` default to `ON` there).

Windows, as CI does it (any other generator works too):

```powershell
cmake -S . -B build/dev -G "Visual Studio 18 2026" -DKB_BUILD_EDITOR=ON -DKB_BUILD_RENDERER=ON -DBUILD_TESTING=ON
cmake --build build/dev --config Release --target kb_cli kb_cli_tests --parallel 4
```

Build only the targets you changed; a full build of the editor is large. `kb_cli.exe` is written to
`build/dev/bin/Release/`. Headless Linux (the CI row `Linux GCC ASan UBSan`):

```sh
cmake -S . -B build/linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DKB_BUILD_EDITOR=OFF \
  -DKB_BUILD_RENDERER=OFF -DBUILD_TESTING=ON -DKB_ENABLE_SANITIZERS=ON
```

First-party targets compile with warnings as errors (`KB_STRICT_OWN_WARNINGS`, on by default).

## Run tests

```powershell
ctest --test-dir build/dev -C Release -N                                # list every test
ctest --test-dir build/dev -C Release -R "^kb_cli_tests$" --output-on-failure   # one test
ctest --test-dir build/dev -C Release -L tools --output-on-failure      # one label
ctest --test-dir build/dev -C Release --label-exclude performance      # skip timing tests
python -m unittest discover -s scripts/tests                            # packaging, notices, localization
python scripts/editor_localization.py check                             # editor texts in all 32 catalogs
kb_cli api-check --baseline tests/api_baseline/script_api.json          # public script API compatibility
```

Run the tests of the module you changed, not the whole suite. A change to editor texts adds the keys to
every catalog in `sources/editor/content/EditorShell/Localization/`. A deliberate script API change updates
the baseline with `kb_cli api-check --baseline tests/api_baseline/script_api.json --update-baseline` in
the same change; a breaking change is reported by the check and is not accepted silently.

A bug fix comes with a test that fails without the fix. New third-party code needs an entry in
`third_party/third_party_manifest.json`, its license text and a section of
`third_party/THIRD_PARTY_LICENSES.md` (`scripts/tests/test_third_party_manifest.py` checks this).
CI (`.github/workflows/ci.yml`) also runs `git diff --check`, so keep whitespace clean.

## Report a bug

A report that can be reproduced states: the engine commit, the operating system and compiler, what you
expected and what happened, and a minimal project built with `kb_cli`, so no editor session is needed:

```powershell
kb_cli new --parent repro --name Repro --template Empty
kb_cli batch --project repro/Repro --file steps.txt --dry-run     # preview what the steps change
kb_cli batch --project repro/Repro --file steps.txt                # all steps or none
kb_cli run --project repro/Repro --frames 60 --json                # headless run: logs, events, errors
```

`steps.txt` has one `kb_cli` command per line, for example `entity add --name Lamp --position 0,3,0` or
`component add --entity Lamp --type Light --set intensity=2`. Attach `steps.txt`, the output of the `run`
command (`--json` prints one result object with the exit code) and, for a crash, the dump or log. Exit
codes: 0 is success, 1 is any failure. See [docs/agentic_scripting.md](docs/agentic_scripting.md) and
`kb_cli help` for every command.

## Write a plugin (provider module)

A provider module is a library that the engine loads through the module ABI
(`sources/engine/include/engine/modules/`): `kEngineModuleAbiVersion` must match, and the library exports
`kb_engine_module_abi_version`, `kb_engine_module_name`, `kb_create_engine_module` and
`kb_destroy_engine_module`. The smallest complete examples are `sources/plugins/ai` (CMake and module) and
`sources/plugins/world_2d` (module, runtime system, tests):

1. Make a folder `sources/plugins/<name>` with a `CMakeLists.txt` like the one in `sources/plugins/ai`: a
   `STATIC` library, or `SHARED` when `KB_BUILD_PROVIDER_MODULES_AS_DLL` is on (the default), linked to
   `kb_engine`, with `KB_ENGINE_MODULE_STATIC_LINK` defined for the static case and `kb_apply_quality_gates`.
2. Derive from `kb::modules::IEngineModule` (see `World2DModule.hpp/.cpp`): `Metadata()` returns the module
   name, version, dependencies and loading phase; `OnSceneAttach` adds your scene systems and
   `OnSceneDetach` removes them.
3. Add a test executable next to it and register it with `add_test` (as `kb_ai_plugin_tests`).
4. Register the folder in the root `CMakeLists.txt` and the built library with the targets that load
   modules (`sources/game/CMakeLists.txt`, `sources/editor/CMakeLists.txt`, `sources/tools/CMakeLists.txt`):
   a project lists its modules by name and library file, and a relative file name is looked up in the
   working directory and around the executable (`EngineModuleLoader::ResolveModulePath`). The test `kb_native_sdk_layout` shows the layout `kb_cli` expects.

For game code that does not need a new engine module, a project can use C++ scripts: `kb_cli script-new
--project <dir> --name <Name> --language cpp` creates one and `kb_cli script-build --project <dir>` builds it
against an engine build that has Release libraries (set `KB_ENGINE_SDK_ROOT` to that build, or run
`kb_cli` from it).

Out-of-tree plugins are not packaged or installed by a separate SDK today: a module is built together with
the engine, as the ones in `sources/plugins` are.

## License

The engine is released under the [MIT License](LICENSE).
