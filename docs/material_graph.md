# KB21 Material Graph

MAT-71 defines the production contract for Material Graph assets used by the editor, scene viewport and runtime renderer.

## Runtime Contract

Material Graph is a GPU material pipeline, not a CPU fallback feature. A production graph path must compile graph source, cook a bgfx shader binary for the active backend, load it through `MaterialProgramRegistry`, bind graph uniforms/textures, submit a real bgfx draw and pass readback tests. Source-only generation, noop submit, or a CPU-flattened material is not proof of support.

The canonical code entry points are:

- `CompileRenderMaterialGraphToShaderSource`
- `CookRenderMaterialGraphShaderArtifact`
- `Renderer::SetGraphShaderCacheRoot`
- `BuildRenderMaterialGraphNodeSupportMatrix`
- `MaterialProgramRegistry`
- `RuntimeMaterialResolver`
- `SceneMeshPassResources`
- `EditorMaterialGraphCookService`
- `kb_standalone_player`

## Render Path Support

| Path | Status | Contract |
|---|---|---|
| `GpuForward` | Production | Main scene/game graph material path. Supports DefaultLit, Unlit, blend modes, graph textures, scene textures, WPO, customized UV0 and displacement as covered by renderer GPU tests. |
| `Preview` | Production | Uses the same cooked graph shader path as scene rendering. Preview vs scene identity is covered by MAT-68 acceptance tests. |
| `GpuShadow` | Production | Cooks/loads generated graph vertex shader when vertex-domain outputs are present and keeps shadow-pass fragment inputs neutral where the pass has no lighting uniforms. |
| `CpuFallback` | Production fallback | Explicit fallback/error path only. It must not be presented as GPU graph proof. |
| `GpuDeferred` | Production | Deferred graph materials cook a distinct `GBuffer` artifact, write albedo/normal/material MRT attachments, and are lit by the deferred lighting pass. Missing `GBuffer` graph programs are diagnostics/errors, not builtin forward proof. |
| Forward+ / clustered lighting | Unsupported | `SceneRenderLightingPath::ClusteredForwardPlus` is declared for roadmap use only; `Forward` and `Deferred` are the production scene lighting paths. |

## Node Support Matrix

`RenderMaterialGraphNodeSupport` is the authoritative enum for node availability:

- `Production`: available in the editor palette and expected to validate, generate shader code, reflect resources and render on GPU for the production forward path.
- `Experimental`: visible only with an explicit warning and never silently counted as production.
- `FallbackOnly`: valid only as an explicit non-production path downgrade.
- `Unsupported`: not a working node and must not be exposed as production.

The source of truth is `BuildRenderMaterialGraphNodeSupportMatrix()`. It returns one row for every node in `AllRenderMaterialGraphNodeKinds()` with `authoringSupport`, `gpuForwardSupport`, `gpuShadowSupport`, `gpuDeferredSupport`, `cpuFallbackSupport`, `previewSupport` and a release note. Renderer tests assert that every enumerated node has a row and that `GpuDeferred` remains production only with the `GBuffer` cook artifact, MRT writer, deferred lighting pass and GPU readback proof.

Current authoring status: every node returned by `AllRenderMaterialGraphNodeKinds()` is `Production` for authoring, forward and preview graph paths. The deferred `GBuffer` graph path is `Production` for material-surface data used by the MRT writer: base color, normal, roughness, metallic, occlusion, emissive, WPO/customized UV0/displacement, material attributes, function/layer stack nodes, custom code, parameter collections, switch nodes, world/object/camera nodes and editor organization nodes. Scene color, scene texture, scene depth and depth fade are rejected for opaque/masked `GpuDeferred` GBuffer geometry, and accepted for transparent `GpuDeferred` materials because the `BaseTransparent` pass binds the scene-color snapshot and GBuffer depth texture.

## Artist Workflow

Create graph assets in the Material Graph editor, then create or save a `.kbmat` material from the graph before assigning it to a mesh. Raw graph assets are source assets; mesh renderers consume material assets. Saving a material requests a graph cook and causes scene meshes using that material to resolve the new graph program on the next render update.

Standalone runtime builds stage Material Graph shader artifacts next to the executable at `.cache/graph_shaders`. `kb_standalone_player` resolves that directory by default, calls `Renderer::SetGraphShaderCacheRoot` before renderer initialization and can launch against a mounted asset root with `--asset-root`, `--mesh`, `--material` and `--expect-graph-gpu-count`. Material graph cooking remains an editor/build step; packaged games load cooked artifacts from the staged cache.

Visible statuses:

- `Ready (GPU graph)`: cooked binary loaded and rendering through the graph program.
- `Compiling`: cook is pending or in flight.
- `Stale (last-good)`: current edit failed or is still cooking, but a previous good graph program remains active.
- `Failed (error material)`: no usable graph binary exists and the explicit error material path is active.
- `Fallback`: the renderer selected an explicit fallback path and should show diagnostics.

## Backend Support

| Backend family | Status | Notes |
|---|---|---|
| D3D11 / `dxbc` | Production | Primary Windows GPU proof path. MAT-68 uses real D3D11 readback. |
| D3D12 / `dxil` | Production shader profile | Prebuilt profile is staged; runtime support follows bgfx backend availability. |
| Vulkan / `spirv` | Production shader profile | Prebuilt profile is staged; runtime support follows bgfx backend availability. |
| OpenGL / `glsl` | Production shader profile | Prebuilt profile is staged; runtime support follows bgfx backend availability. |
| GLES / `essl` | Production shader profile | Prebuilt profile is staged; runtime support follows bgfx backend availability. |
| Apple/Metal | Unsupported | Windows cannot regenerate bgfx `metal` shaders. Existing `prebuilt_shaders/metal` are stale after varying signature changes from the UV/object-space work, so graph materials must not be claimed production on Apple until Metal shaders are regenerated on macOS and added to GPU coverage. |

Metal release requirement: regenerate every `sources/renderer/prebuilt_shaders/metal/*.bin` on macOS or another working Metal shader toolchain, stage the artifacts, run a Metal graph-material link/render test, then change this table and the release gate expectation in the same change.

## Release Gate

Run:

```powershell
.\tests\run-material-graph-release-gate.ps1 -BuildDir build/ci-fix -Config Debug -SkipConfigure
```

The gate builds and runs renderer tests, editor tests, the editor executable and `kb_standalone_player --self-test`, then performs a no-token smoke over the Material Graph code surface. The renderer test binary includes `GraphForwardGpuRenderTests`, the MAT-68 graph acceptance suite, deferred `GBuffer` MRT readback coverage and real D3D11 GPU readback coverage, so it is the required visual GPU proof for Material Graph releases.

Use `-IncludeWindowSmoke` to also run `run-render-smoke.ps1` for the broader bgfx window sample after the Material Graph gate has passed.
