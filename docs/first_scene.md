# From an empty project to an exported scene

This Windows x64 tutorial creates a camera, light, environment, floor and cube,
attaches a generated Lua behaviour, renders a preview and exports a signed
Release package. No scene or project data is patched by hand.

Use a configured Release build containing `kb_cli`, `kb_game` and the runtime
providers. Run from the engine root in an MSVC developer PowerShell with Python
and CMake on `PATH`. Packaging builds any missing targets.

```powershell
$env:CMAKE_BUILD_PARALLEL_LEVEL = "2"
python scripts/run_first_scene_tutorial.py --build-root build/release --workspace "$env:TEMP/21kb-FirstScene"
```

Replace `build/release` with your CMake build directory. Choose a new workspace
outside any Git repository: private signing keys cannot be written into a
repository. Existing tutorial output is never overwritten.

The executable recipe is [first_scene.json](tutorials/first_scene.json). Each
array is one command and its arguments. The runner prints expanded commands and
saves their output in `step-NN.log`:

1. `kb_cli new` creates `SampleScene` using `Empty`. Its descriptor is
   `SampleScene.21kbproject`; its startup scene is `Assets/Scenes/Main.21kbscene`.
2. `kb_cli entity add` creates Camera, Directional Light, Environment, Plane and
   Cube presets. `kb_cli component set` aims the camera at the cube.
3. `kb_cli script-new` creates `FirstScript.lua`; `kb_cli scene-attach` attaches
   it to Cube. Edit this ordinary Lua file to add behaviour. The tutorial uses
   the generated behaviour without an unexplained script fixture.
4. `kb_cli validate` takes the Lua file, not a project directory. `kb_cli run`
   checks ten runtime frames. The development player renders `preview.png`
   through a hidden D3D11 host.
5. `kb_cli keys generate` creates `Tutorial.kbkey` beside the project.
   `scripts/package_game.py` exports Release using the explicit project descriptor,
   product metadata and key. Keep this key private and retain it for updates;
   it is excluded from the exported payload. Pack signing is separate from
   Windows Authenticode signing.
6. The runner temporarily moves the source project aside and runs the exported
   player for thirty rendered frames, then restores the project. `result.json`
   records success and the executable's path under `Export`.

Open `SampleScene/SampleScene.21kbproject` in the editor. Select Cube in Hierarchy
to edit its components in Inspector; Play runs its Lua behaviour. CLI authoring
commands save immediately.

If a step fails, inspect its log and retry in a new workspace. A failed final
player check restores the source project before reporting the error. Partial
runs do not produce a success report.
This tutorial verifies Windows/D3D11 only. See the
[feature catalog](feature_catalog.json) and [platform matrix](library_support_matrix.md)
for other verification scopes.
