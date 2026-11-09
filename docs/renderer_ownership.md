# Renderer ownership and effect contracts

The scene stores authored components, asset identifiers and runtime simulation state. It does not own bgfx handles.
`EcsRenderSceneSynchronizer` extracts render proxies into `RenderSceneStore`; the renderer owns those derived copies.
The editor and player submit through the same renderer. Editor panels do not participate in runtime resource ownership.

| Owner | Resources | Lifetime boundary |
| --- | --- | --- |
| Scene asset manager | Decoded CPU assets and published asset revisions | Asset unload/reload; consumers retain shared data while using it |
| Runtime resource cache and scene bindings | Asset-to-render-resource associations | `Renderer::ReleaseScene` removes that scene's associations; shared assets remain owned by their resource users |
| `RenderResourceRegistry` | Mesh buffers, texture handles, material resources and immutable CPU mesh proxies | Generational handles and resource versions reject stale references; deferred destruction retires queued GPU work |
| `Renderer` | Frame plans, viewport targets, shader programs, light/shadow resources and rendering helpers | Initialization/shutdown and viewport target recreation; effect resources are independent of editor windows |
| `SceneGiHistory` | Per-viewport copied lit color and two GI/AO accumulation targets | Extent/format changes recreate targets; owner/camera/effect changes and frame gaps invalidate sampled history |
| `SceneGiVoxelGrid` | Fixed 64-cubed CPU/GPU occupancy and bounded mesh masks | Scene changes rebuild occupancy; streamed-out meshes are pruned; scene release clears masks; shutdown releases buffers |
| `SceneSkyReflection` | Capture cube, two prefiltered cubes and temporary face framebuffers | One capture/prefilter step per frame; only a completed mip chain is published; shutdown destroys its handles |
| bgfx | Backend objects, command queues, synchronization and native graphics API resources | Engine handles enqueue work; backend completion controls when queued work can retire |

No component should destroy a handle owned by another row. Asset identifiers are not bgfx handle indices. Registry payload
statistics exclude targets owned by effect helpers and driver padding; adding overlapping bgfx and registry estimates would
double-count resources. A package installs its shader provider before rendering; a missing package shader fails instead of
searching editor or source directories.

`scripts/tests/test_module_boundaries.py` checks include direction. The renderer may depend on the engine and bgfx; the engine
core, network and HTTP modules do not depend on the renderer or editor. The runtime asset-pack validator is an explicit tool
exception above the engine core. `kb_renderer_tests` and the isolated player image matrix execute without an editor library
or editor process supplying runtime services.

## Effect resource and cost contracts

Effects share the frame graph and shader profiles. Runtime booleans and uniforms select combinations inside existing passes;
do not generate a separate program for every combination of independent toggles. An optional effect must load its optional
program when first needed, report an unavailable dependency and leave unrelated rendering operational.

The rows below use the shared fixed-shader inventory: deferred lighting includes SSR/fog, `fs_gi_resolve.sc` gathers GI/AO,
and `fs_sky_capture.sc` / `fs_sky_prefilter.sc` build sky reflections. Windows.x64 packages select DXBC, DXIL and SPIR-V;
development OpenGL selects GLSL. The moving reference scene qualifies that host matrix; the detailed effect lifecycle
readbacks currently qualify D3D11. Other generated profiles are compilation artifacts until exercised on their actual host.
Adding a profile to a manifest must not automatically label its effects as runtime-verified.

| Effect | Inputs and owner | History and fallback | Work and payload scope | Verification entry point |
| --- | --- | --- | --- | --- |
| SSR | G-buffer depth/normal, previous lit color; `SceneGiHistory` and deferred lighting | Missing/offscreen data or roughness above 0.7 uses the environment; camera cuts invalidate history even without TAA | Up to 32 ray steps per shaded pixel; shared lit copy plus GI/AO targets, no separate SSR permutation | `kb_renderer_tests screen-effect-lifecycle` and `screen-space` |
| Screen-space GI and AO | G-buffer and finished HDR color; `SceneGiResolvePass` | Per-viewport reprojection; owner, projection kind, effect mode, frame gap, cut and resize invalidate history | Two RGBA16F GI and two RG16F AO targets: 24 bytes per pixel, plus one lit copy in scene color format; gather/resolve timing depends on resolution | `kb_renderer_tests gi-resolve` and `screen-effect-lifecycle` |
| Voxel GI | CPU triangle proxies or diagnosed bounding-box fallback, albedo/emissive, up to eight packed lights | Occupancy follows geometry/material/grid origin; lighting is evaluated from the current light uniforms | 64 cubed RGBA8 + RGBA16F = 3 MiB GPU payload and 3 MiB CPU voxel payload; mask cache at most 8 MiB, plus one mask's construction scratch and bgfx upload staging | `kb_renderer_tests renderer-diagnostics`, `screen-space` and `screen-effect-lifecycle` |
| Sky reflections | Backdrop or environment texture; `SceneSkyReflection` | Incomplete or unavailable capture uses the environment color; this is sky capture, not local scene-probe capture | Three 128-face RGBA16F cubemaps with mip chains, six temporary face targets; one capture step and eight prefilter steps | `kb_renderer_tests sky-reflection` |
| Height and volumetric fog | Scene depth, height parameters and lights; deferred lighting and ordinary forward transparent surfaces | Explicit Desktop/Mobile/Web policy; fog itself has no temporal buffer | Shared analytic extinction or a 24-step, 100-metre march; no additional history texture | `kb_renderer_tests screen-space`, `fog-profiles`; [profile contract](renderer_fog.md) |

`SceneGiVoxelGrid::LastUpdateStats` reports CPU update time, rebuild count, mask builds, resident payload estimates and bytes
queued for upload. An unchanged signature performs no voxel rebuild/upload. The mask budget evicts cached masks and rebuilds
them when required; it never replaces their geometry with solid boxes. CPU time is measured, not a hard real-time deadline.
The small streaming fixture has a 25 ms p95 rebuild guard; it is not a throughput promise for arbitrary imported meshes.
Allocator/node overhead, temporary mask construction, queued upload copies and driver allocations are separate from resident
payload estimates. Profiling instructions and GPU query timing scope are in [Renderer profiling](renderer_profiling.md).

Local reflection-probe fields currently contribute statistics but are not sampled by the shading programs. They must not be
described as rendered local probes, blending or parallax correction until that path has image evidence. Fog Mobile/Web
policy selection has native fallback-image tests; desktop execution of those policies does not qualify a mobile GPU or browser.

## Adding or changing an effect

Record the effect's inputs, resource owner, format/extent, pass ordering, capability requirements, shader profiles, invalidation
rules and fallback before extending the pipeline. State its fixed storage bounds and measured CPU/GPU scopes. Use existing
targets and pass variants when their lifetimes and formats match; otherwise document why an extra allocation/program is needed.
Add real draw/readback tests for enable/disable, resize, camera/scene replacement and resource unload. For authored effects,
exercise save/reload/cook and an isolated package. Include a negative control that would fail if the effect or its invalidation
were removed. Record untested backends as untested and keep exact backend/source/binary identity with the artifacts.
