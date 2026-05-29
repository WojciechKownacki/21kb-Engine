# 21kb Engine

**21kb Engine is in early development.**

This repository is a work-in-progress foundation for a small, native,
cross-platform game and editor engine targeting Windows, Linux and macOS.
The project is not production-ready yet. APIs, folder layout, build targets and
runtime architecture may change quickly while the core direction is being
validated.

## Current Direction

21kb Engine is built around a native C++ core with a strong focus on:

- high performance CPU-side systems,
- clean engine ownership boundaries,
- scalable editor workflows,
- dockable and detachable editor panels,
- minimal dependency surface,
- explicit renderer, ECS, physics and audio integration layers,
- avoiding heavyweight webview-based editor shells and generic UI frameworks.

The editor is currently a native executable with a first Win32 platform backend
and an internal docking/layout model. The long-term goal is to keep the editor
fast, modular and deeply integrated with the engine rather than wrapping the
engine in an external application framework.

## Current Status

Implemented foundations:

- CMake-based C++20 project structure,
- `kb_engine` static library target,
- first post-process pipeline module,
- native `kb_editor` executable,
- native editor shell with viewport, hierarchy, inspector, assets and console
  regions,
- third-party dependency area with license documentation.

Vendored third-party libraries currently include:

- bgfx,
- Flecs,
- miniaudio,
- Jolt Physics,
- Box2D.

## Build

Configure:

```sh
cmake -S . -B build
```

Build the editor:

```sh
cmake --build build --config Debug --target kb_editor
```

Run on Windows:

```powershell
.\build\editor\Debug\kb_editor.exe
```

## License

21kb Engine is licensed under the MIT License. Third-party dependencies remain
under their own licenses. See:

- `LICENSE`
- `third_party/THIRD_PARTY_LICENSES.md`
