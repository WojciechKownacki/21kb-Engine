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

### Graph texture dimensions

Texture dimension is asset metadata carried unchanged through loading, the runtime cache and the bgfx resource. Legacy `.kbtex` files and ordinary images remain `Texture2D`; text assets may declare `dimension cube`, `dimension 3d` with `depth > 1`, or `dimension 2dArray` with `layers > 1`. Imported image containers derive Cube, 3D and 2D Array from bimg metadata. The runtime uploads the complete RGBA8 LOD0 for every cube face, volume slice or array layer; legacy mip levels remain intentionally ignored.

Every reflected graph sampler declares its expected dimension. Submit never binds a resource of a different dimension: it records `TextureDimensionMismatch` with the texture asset id and expected/actual dimensions, increments `textureDimensionMismatchCount`, and binds a white or normal fallback created with the expected Cube, 3D or 2D Array type. A mismatch therefore remains visible and deterministic without reaching the backend as an invalid sampler/resource pair.

## Render Path Support

| Path | Status | Contract |
|---|---|---|
| `GpuForward` | Production | Main scene/game graph material path. Supports DefaultLit, Unlit, blend modes, graph textures, scene textures, WPO, customized UV0 and displacement as covered by renderer GPU tests. |
| `Preview` | Production | Uses the same cooked graph shader path as scene rendering. Preview vs scene identity is covered by MAT-68 acceptance tests. |
| `GpuShadow` | Production | Cooks/loads the generated graph vertex shader for WPO/displacement and evaluates graph alpha with clip for masked materials. A fixed position-only/opaque builtin is allowed only when the graph changes neither position nor alpha; otherwise a missing artifact fails closed with diagnostics. |
| `CpuFallback` | Production fallback | Explicit fallback/error path only. It must not be presented as GPU graph proof. |
| `GpuDeferred` | Production | Deferred graph materials cook a distinct `GBuffer` artifact and write four color MRTs: albedo, normal, material parameters plus stable shading-model id, and HDR emissive plus explicit specular. Deferred lighting bypasses PBR for Unlit and adds emissive after PBR for lit models. Missing `GBuffer` graph programs are diagnostics/errors, not builtin forward proof. |
| Forward+ / clustered lighting | Production | `SceneRenderLightingPath::ClusteredForwardPlus` uses the forward graph material pass with the expanded Forward+ light budget and exposes `forwardPlus` through `ShadingPathSwitch`; renderer/editor tests cover project settings, preview inheritance and GPU shader-path selection. |

## Node Support Matrix

`RenderMaterialGraphNodeSupport` is the authoritative enum for node availability:

- `Production`: available in the editor palette and expected to validate, generate shader code, reflect resources and render on GPU for the production forward path.
- `Experimental`: visible only with an explicit warning and never silently counted as production.
- `FallbackOnly`: valid only as an explicit non-production path downgrade.
- `Unsupported`: not a working node and must not be exposed as production.

The source of truth is `BuildRenderMaterialGraphNodeSupportMatrix()`. It returns one row for every node in `AllRenderMaterialGraphNodeKinds()` with `authoringSupport`, `gpuForwardSupport`, `gpuShadowSupport`, `gpuDeferredSupport`, `cpuFallbackSupport`, `previewSupport` and a release note. Renderer tests assert that every enumerated node has a row and that `GpuDeferred` remains production only with the `GBuffer` cook artifact, MRT writer, deferred lighting pass and GPU readback proof.

Current authoring status: every node returned by `AllRenderMaterialGraphNodeKinds()` is `Production` for authoring, forward and preview graph paths. The deferred `GBuffer` graph path is `Production` for material-surface data used by the MRT writer: base color, normal, roughness, metallic, occlusion, HDR emissive, explicit specular, stable shading model, WPO/customized UV0/displacement, material attributes, function/layer stack nodes, custom code, parameter collections, switch nodes, world/object/camera nodes and editor organization nodes. Unlit resolves to base color plus emissive without PBR; lit models use the same `0.08 * specular` dielectric F0 convention as Forward and add emissive after lighting. Scene color, scene texture, scene depth and depth fade are rejected for opaque/masked `GpuDeferred` GBuffer geometry, and accepted for transparent `GpuDeferred` materials because the `BaseTransparent` pass binds the scene-color snapshot and GBuffer depth texture.

## Artist Workflow

Create graph assets in the Material Graph editor, then create or save a `.kbmat` material from the graph before assigning it to a mesh. Raw graph assets are source assets; mesh renderers consume material assets. Saving a material requests a graph cook and causes scene meshes using that material to resolve the new graph program on the next render update.

Standalone runtime builds stage Material Graph shader artifacts next to the executable at `.cache/graph_shaders`. `kb_standalone_player` resolves that directory by default, calls `Renderer::SetGraphShaderCacheRoot` before renderer initialization and can launch against a mounted asset root with `--asset-root`, `--mesh`, `--material` and `--expect-graph-gpu-count`. Material graph cooking remains an editor/build step; packaged games load cooked artifacts from the staged cache.

### Artifact identity

Cook and runtime use the same canonical variant key and path:
`graph_<sourceHash>/variant_<variantKey>/<pass>/<backend>/{fs,vs}.bin`. The variant key includes the graph source, wrapper version, entry point and complete reflection that can alter generated wrapper code or binary resources. Consequently Opaque/Masked, DefaultLit/Unlit and other wrapper-changing variants with the same graph source coexist instead of overwriting one another. Manifest version 2 stores the variant key; the legacy reader remains available for old manifests but refuses an ambiguous source/pass/backend lookup.

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
| Apple/Metal | Production shader profile | The Metal prebuilt profile is regenerated and staged with the shared GBuffer contract; runtime support follows bgfx backend availability. A native Metal GPU readback remains platform-specific validation rather than a Windows release-gate claim. |

## Release Gate

Run:

```powershell
.\tests\run-material-graph-release-gate.ps1 -BuildDir build/ci-fix -Config Debug -SkipConfigure
```

The gate builds and runs renderer tests, editor tests, the editor executable and `kb_standalone_player --self-test`, then performs a no-token smoke over the Material Graph code surface. The renderer test binary includes `GraphForwardGpuRenderTests`, the MAT-68 graph acceptance suite, deferred `GBuffer` MRT readback coverage and real D3D11 GPU readback coverage, so it is the required visual GPU proof for Material Graph releases.

Use `-IncludeWindowSmoke` to also run `run-render-smoke.ps1` for the broader bgfx window sample after the Material Graph gate has passed.
