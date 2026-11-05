# Windows test distribution

Build the required Release targets incrementally, then stage the existing outputs:

```powershell
python scripts/stage_windows_preview.py --build-root build/bgfx-update --output WindowsPreview
```

The output must be new or empty. Staging copies the editor, player, packaging tools,
providers, shader compiler, Python runtime, public headers, native libraries and
licenses. It does not copy projects, user settings, build caches or source-machine
toolchain paths into a CMake cache. `prebuilt-engine.json` records every staged
file's SHA-256. Export verifies those inputs before and after packaging; missing
or modified tools fail without falling back to a source build.

Run `bin/kb_editor.exe`. Build Game resolves this installation beside the editor;
its build-directory setting defaults to the installation root. A configured
compatible MSVC/CMake environment is required for C++ modules. The Python builder
used by Build Game must be available. Lua-only authoring does not compile modules.
The installed engine binaries are Release builds; Development/Release in Build
Game selects the package policy, signing and development metadata.

Only Windows x64 export is supported by this distribution. A Release package
still requires the existing signing configuration. The normal source-checkout
export path continues to build its requested CMake targets.

## Verified scope

The following workflows have Windows headless editor/runtime evidence. Graphics
acceptance uses D3D11 on the qualification host; shader compilation for another
backend does not establish its runtime support.

| Area | Available workflow | Principal limits |
| --- | --- | --- |
| C++ and Lua | Create, compile project modules, attach, edit, run and export; CLI/MCP API discovery | Compatible MSVC/CMake environment required; IDE commands exercised with a controlled launcher. Rebuild external modules for native API 6 / engine module ABI 26 |
| Builtin materials | Numeric sheen, transmission and anisotropy with save/cook support | Advanced texture maps unsupported; screen-space transmission has no offscreen or multiple-surface refraction; environment reflections remain isotropic |
| Lighting | Two local cube probes with bounds/parallax/blending; static diffuse lightmap bake; profile DOF | RGBA8/BGRA8 probe inputs; axis-aligned bounds; direct LDR bake with unique UV0, no unwrap/indirect bounce; DOF requires single-sample scene depth and disabled MSAA |
| Physics | Jolt ragdoll bindings, grid cloth, wheeled vehicles, box-cell fracture, flat-plane buoyancy | Authored capsule/point-joint rig; bounded grid and fracture assets; no arbitrary imported-mesh fracture or wave sampling; active assemblies refuse snapshots |
| Particles | World, SDF and heightfield collision through the existing CPU backend | Bounded fields/travel; geometry collision rejects GPU-required effects |
| 2D | Isometric/axial-hex maps; deformable polygon/line assets; weighted planar rigs and clips | Simple polygons without holes; butt caps/bevel joins; hex border brushes and nonrectangular platformer navigation unsupported |
| Audio | CPU HRTF and supported AmbiX decoding with observable PCM | See `kb_cli api --topic SpatialAudio` for format and channel requirements |
| Runtime UI | RGB MSDF, semantic accessibility, Windows UIA actions/SAPI speech, decoded video/audio synchronization | Narrator itself was not exercised; TalkBack/VoiceOver have no host qualification |
| CSG | Union/subtract/intersect into ordinary render and collision assets | Closed inputs and bounded brush/mesh budgets; attachment requires a static ordinary mesh owner |
| Python authoring | Embedded CPython 3.14.10 scene/property operations with undo, save and error reporting | Owner-thread authoring interface; the interpreter is part of the Windows editor distribution |
| Editor extensions | Public C ABI for external actions and text/button panels, install/unload | Minimal panel surface; Windows x64 host qualification |
| Motion input | Common sensor model, replay and Windows provider lifecycle | Physical accelerometer/gyroscope measurements were unavailable and remain unqualified |

Use `kb_cli api --search <term>` for each feature's detailed contract and budgets.
This preview was qualified on the current Windows host. A separate clean-machine
installation remains unverified. Non-Windows editor/player hosts, hardware ray
tracing and temporal upscaling are outside the available Windows preview scope.
