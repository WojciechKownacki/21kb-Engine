# Render Fix Tasks

## GPU-driven render pipeline

- [x] Separate CPU-side GPU-driven candidate/stat plumbing from real GPU dispatch execution in public runtime stats and documentation.
- [x] Add explicit GPU-driven feature state enum: Disabled, CpuValidationOnly, ComputeCulling, IndirectDrawSubmit, and MeshletSubmit.
- [x] Add runtime GPU culling pass scheduling to the render frame pipeline.
- [ ] Create GPU-visible instance, bounds, meshlet, LOD, and draw-command buffers.
- [x] Upload per-frame culling input data without stalling the CPU.
- [ ] Build hierarchical Z buffer inputs from the current depth buffer.
- [ ] Dispatch HZB seed and downsample compute passes.
- [x] Dispatch instance frustum culling on GPU.
- [ ] Dispatch instance occlusion culling against HZB.
- [ ] Compact visible instances into a GPU-visible draw list.
- [ ] Generate indirect draw arguments on GPU.
- [ ] Submit indirect draws from generated draw arguments.
- [x] Add fallback path when indirect draws or compute shaders are unavailable.
- [ ] Dispatch meshlet or cluster culling for GPU-driven meshes.
- [ ] Cull meshlets by frustum, cone backface, and occlusion data.
- [ ] Select LOD on GPU using screen coverage and per-mesh thresholds.
- [ ] Keep CPU LOD selection as validation and fallback.
- [x] Add deterministic CPU/GPU parity validation for culling, LOD selection, meshlet ranges, and dropped-instance budgets.
- [ ] Expose GPU culling, indirect draw, meshlet, and LOD counters in submit stats.
- [x] Expose whether GPU-driven counters came from CPU candidates, GPU dispatch counters, or debug readback.
- [ ] Add GPU culling debug readback for visible instance counts.
- [x] Add tests for GPU-driven capability gating.
- [x] Add tests for indirect draw fallback behavior.
- [x] Add render smoke coverage for GPU-driven and CPU fallback paths.

## Production PBR materials

- [ ] Add diagnostics for advanced material fields that are parsed but ignored by the current runtime shader path.
- [ ] Add a renderer-visible unsupported-material-feature bitset for clearcoat, sheen, transmission, subsurface, anisotropy, decals, layers, and future extensions.
- [ ] Fail or warn when material assets request unsupported features without an explicit fallback policy.
- [ ] Pack clearcoat material factors into shader uniforms or material buffers.
- [ ] Bind clearcoat and clearcoat roughness textures in the mesh submit path.
- [ ] Implement clearcoat BRDF lobe in the lit mesh shader.
- [ ] Pack sheen color and roughness into shader inputs.
- [ ] Bind sheen color textures.
- [ ] Implement sheen BRDF contribution.
- [ ] Pack transmission, thickness, attenuation color, and attenuation distance.
- [ ] Bind transmission and thickness textures.
- [ ] Implement transmission lighting path for transparent materials.
- [ ] Pack subsurface color and subsurface factor.
- [ ] Implement a subsurface approximation for forward lighting.
- [ ] Pack anisotropy strength and rotation.
- [ ] Bind anisotropy textures.
- [ ] Implement anisotropic specular in the PBR shader.
- [ ] Bind decal textures and decal blend mode.
- [ ] Implement decal projection or mesh-surface decal blending.
- [ ] Bind layer mask textures and layer blend mode.
- [ ] Implement layered material blending.
- [ ] Add material variant keys for advanced PBR feature combinations.
- [ ] Add validation for unsupported material feature combinations.
- [ ] Add material asset versioning and migration for newly introduced PBR feature fields.
- [ ] Add inspector/debug output that shows which material features are active, unsupported, or falling back.
- [ ] Add shader tests or render smoke scenes for clearcoat, sheen, transmission, subsurface, anisotropy, decals, and layered materials.
- [ ] Add asset-loader tests for every advanced texture path and factor.

## IBL pipeline

- [ ] Add environment asset type for HDR/cubemap source data.
- [ ] Generate irradiance cubemaps for diffuse IBL.
- [ ] Generate prefiltered radiance cubemaps for GGX specular IBL.
- [ ] Generate or load BRDF integration LUT.
- [ ] Add runtime resource ownership for IBL textures.
- [ ] Bind irradiance map, prefiltered environment map, and BRDF LUT in scene submit.
- [ ] Sample irradiance map for diffuse environment lighting.
- [ ] Sample prefiltered environment map by roughness for specular reflections.
- [ ] Use BRDF LUT for split-sum specular IBL.
- [ ] Add reflection probe resource registration.
- [ ] Add reflection probe extraction from scene data.
- [ ] Select the most relevant reflection probes per view or per object.
- [ ] Blend local reflection probes.
- [ ] Implement box-probe parallax correction.
- [ ] Implement sphere-probe parallax correction.
- [ ] Add fallback to hemisphere or constant environment when IBL resources are missing.
- [ ] Expose IBL, probe, and parallax stats in runtime stats.
- [ ] Add tests for probe selection and fallback behavior.
- [ ] Add render smoke coverage for IBL and local probes.

## Robust render graph

- [ ] Split render graph building, graph compilation, and graph execution into explicit stages.
- [ ] Make the render graph executor own bgfx view order generation instead of patching view order manually after graph compile.
- [ ] Add typed pass parameter structs so every pass binds inputs, outputs, uniforms, samplers, and debug names through one validated contract.
- [ ] Add a render graph blackboard for per-frame view, lighting, temporal, exposure, and editor data shared between passes.
- [ ] Ensure every frame resource is declared through the render graph.
- [ ] Ensure every pass declares all read and write resources.
- [ ] Declare dynamic bloom mip resources and views as graph resources/passes instead of appending mip view ids outside the graph.
- [ ] Declare per-viewport temporal history read/write textures through the graph with explicit external/history lifetimes.
- [ ] Declare exposure histogram/readback resources through the graph with a distinct debug-readback lifetime.
- [ ] Register native-window, swap-chain, editor viewport, and backbuffer textures as external graph resources.
- [ ] Move physical transient texture allocation behind graph compile output.
- [ ] Allocate transient resources from graph lifetime ranges.
- [ ] Reuse alias slots for non-overlapping transient resources.
- [ ] Prevent aliasing when formats, dimensions, sample counts, or usage flags are incompatible.
- [ ] Prevent aliasing across temporal history, readback, native-window, and imported external resources.
- [ ] Emit barriers for every required access transition.
- [ ] Map graph barriers to bgfx view ordering and resource usage constraints.
- [ ] Validate read-before-write and write-after-write hazards.
- [ ] Validate write-after-read hazards, read-after-read compatibility, read/write same-pass rules, and external-resource state assumptions.
- [ ] Validate pass ordering across shadow, scene, post-process, overlay, and final composite passes.
- [ ] Add graph-driven pass culling for disabled or unused passes.
- [ ] Add graph-driven optional pass pruning for disabled bloom, TAA, selection, exposure, shadows, overlays, and debug views.
- [ ] Add graph debug dump for pass order, resources, lifetimes, aliases, and barriers.
- [ ] Add machine-readable render graph dump for CI artifacts and regression diffs.
- [ ] Add in-app debug visualization for resource lifetime.
- [ ] Add in-app debug visualization for alias slots.
- [ ] Add in-app debug visualization for pass timing and target memory.
- [ ] Add CPU pass profiling timestamps.
- [ ] Add GPU pass timing where supported by bgfx.
- [ ] Expose render graph compile stats in renderer runtime stats.
- [ ] Add unit tests for alias compatibility rules.
- [ ] Add unit tests for barrier generation.
- [ ] Add unit tests for pass culling and disabled pass validation.
- [ ] Add regression tests for multi-viewport graph compilation.

## Lighting

- [ ] Keep directional, point, and spot forward lighting covered by runtime tests.
- [ ] Add physically plausible attenuation controls for point and spot lights.
- [ ] Add tiled forward lighting path for many local lights.
- [ ] Add clustered forward+ lighting path for many local lights.
- [ ] Add deferred lighting path option.
- [ ] Add visibility-buffer lighting path option.
- [ ] Add renderer capability selection between forward, clustered forward+, deferred, and visibility-buffer paths.
- [ ] Build per-tile or per-cluster light lists on GPU.
- [ ] Add GPU buffers for light records, light grids, and light indices.
- [ ] Add CPU fallback light list construction for low-end or compute-disabled devices.
- [ ] Support hundreds of visible local lights without per-object uniform limits.
- [ ] Support thousands of culled scene lights with bounded GPU memory.
- [ ] Add debug counters for total lights, visible lights, clustered lights, and lights skipped by capacity.
- [ ] Add debug visualization for tiled or clustered light occupancy.
- [ ] Add stress test scene with hundreds of point and spot lights.
- [ ] Add stress test scene with thousands of culled lights.
- [ ] Add area rectangle light shading.
- [ ] Add area disk light shading.
- [ ] Add tube light shading.
- [ ] Add physically based LTC lookup tables for area lights.
- [ ] Bind LTC lookup textures for area light evaluation.
- [ ] Add area light diffuse and specular integration in the lit shader.
- [ ] Add area light shadowing strategy or explicit unsupported-feature diagnostics.
- [ ] Add per-light shadow enable and disable coverage for all shadow-capable lights.
- [ ] Add shadow atlas allocation for multiple shadow-casting lights.
- [ ] Add point light shadow map support.
- [ ] Add spot light shadow map support.
- [ ] Add cascaded directional shadows.
- [ ] Add stable cascade snapping tests.
- [ ] Add PCSS, EVSM, or MSM implementation beyond stats/config values.
- [ ] Add contact shadow rendering.
- [ ] Add volumetric light scattering rendering.
- [ ] Add volumetric froxel grid or raymarch pass.
- [ ] Add volumetric temporal reprojection and history rejection.
- [ ] Add volumetric quality settings and low-end fallback.
- [ ] Add clustered forward light assignment on GPU.
- [ ] Add clustered light list buffers and debug stats.
- [ ] Add fallback from clustered forward to simple forward lighting.
- [ ] Add exposure-meter tests for advanced light types.
- [ ] Add render smoke scenes for directional shadows, local lights, area lights, contact shadows, and volumetrics.

## Shadow system

- [ ] Split shadow setup into a reusable shadow system instead of a single directional planner.
- [ ] Allocate a shadow atlas for directional, spot, and point shadow maps.
- [ ] Pack multiple shadow-casting lights into the atlas.
- [ ] Track atlas tile allocation, eviction, and reuse.
- [ ] Add per-light shadow cache keys.
- [ ] Reuse unchanged shadow maps across frames when per-light caching is enabled.
- [ ] Invalidate cached shadow maps when caster transforms, caster meshes, light transforms, or shadow settings change.
- [ ] Add cascaded shadow maps for directional lights.
- [ ] Compute cascade split distances from camera near/far and shadow distance.
- [ ] Stabilize cascade projection with texel snapping.
- [ ] Blend or fade between cascades.
- [ ] Add per-cascade culling of shadow casters.
- [ ] Add cascade debug visualization.
- [ ] Implement EVSM shadow filtering.
- [ ] Implement MSM shadow filtering.
- [ ] Implement PCSS blocker search and penumbra filtering.
- [ ] Add shadow filter shader variants and capability fallbacks.
- [ ] Add shadow bias controls per light type.
- [ ] Add normal bias and slope-scaled bias controls.
- [ ] Add receiver-side shadow fade by distance.
- [ ] Add runtime stats for atlas usage, cached shadow hits, cached shadow misses, cascade count, and filter cost.
- [ ] Add unit tests for shadow atlas allocation.
- [ ] Add unit tests for shadow cache invalidation.
- [ ] Add unit tests for cascade split and stable snapping.
- [ ] Add render smoke coverage for CSM, atlas packing, EVSM/MSM/PCSS, and cache reuse.

## Global illumination

- [ ] Add GI mode selection to renderer runtime settings.
- [ ] Implement SSGI screen-space tracing pass.
- [ ] Add SSGI denoise pass.
- [ ] Add SSGI temporal accumulation.
- [ ] Add SSGI fallback to ambient or IBL on low-end devices.
- [ ] Add DDGI probe volume component or scene data.
- [ ] Add DDGI probe irradiance and visibility textures.
- [ ] Add DDGI probe update pass.
- [ ] Add DDGI probe relocation or validity handling.
- [ ] Add DDGI sampling in the lit shader.
- [ ] Add static probe grid GI mode.
- [ ] Add probe grid baking or import path.
- [ ] Add lightmap asset type.
- [ ] Add lightmap UV and texture binding path for static meshes.
- [ ] Add lightmap sampling in material shaders.
- [ ] Add mixed lighting path for baked GI plus dynamic direct lighting.
- [ ] Add GI quality presets.
- [ ] Add low-end fallback from DDGI or SSGI to probe grid, lightmaps, IBL, or constant ambient.
- [ ] Add GI debug visualization for probes, irradiance, visibility, and screen-space traces.
- [ ] Add runtime stats for GI mode, probe count, updated probes, SSGI ray count, and fallback reason.
- [ ] Add tests for GI mode selection and fallback.
- [ ] Add render smoke coverage for SSGI, DDGI or probe grid, and lightmaps.

## Post-Process / HDR

- [ ] Add HDR pipeline validation that scene color, bloom, post-process final, and display output formats are compatible.
- [ ] Add validation that scene HDR targets, post-process intermediates, temporal history, and final composite targets use compatible color spaces.
- [ ] Add explicit low-end non-HDR fallback path.
- [ ] Add automatic fallback from HDR post-process to LDR-compatible output when HDR targets are unsupported.
- [ ] Add exposure histogram compute pass instead of readback-only metering.
- [ ] Keep fragment/readback exposure path clearly marked as transitional, with diagnostics that it is not the production GPU histogram path.
- [ ] Add GPU luminance downsample chain from HDR scene color for exposure, bloom thresholds, and debug visualization.
- [ ] Add compute shader histogram accumulation from downsampled luminance, with configurable bin range and percentile clipping.
- [ ] Add auto-exposure source selection between scene-light estimate, HDR readback histogram, and full GPU histogram path.
- [ ] Add async GPU exposure readback scheduling without frame stalls.
- [ ] Store exposure history per viewport, per scene/camera identity, and per post-process profile instead of as one global renderer state.
- [ ] Add eye adaptation settings for bright and dark adaptation per scene.
- [ ] Add temporal exposure adaptation with min/max EV, separate up/down speeds, and metering mode controls.
- [ ] Reset temporal exposure history on camera cuts, render target recreation, scene changes, and exposure mode switches.
- [ ] Reset temporal exposure history on viewport detach, close, reopen, minimize/restore, render-size change, and backend reset.
- [ ] Reset or invalidate TAA history on camera cuts, projection changes, jitter mode changes, render target recreation, scene changes, and post-process quality changes.
- [ ] Track temporal history with per-viewport generation ids instead of only global frame parity.
- [ ] Add camera-cut and discontinuity detection inputs to render submission.
- [ ] Add motion-vector validity metadata so TAA can reject history when velocity/depth inputs are missing or stale.
- [ ] Add exposure debug view for histogram, metered luminance, adapted luminance, and exposure stops.
- [ ] Add filmic tonemapping with production-ready ACES, neutral or AgX-style, and art-directed curve options.
- [ ] Add tonemap controls for shoulder, toe, saturation, contrast, white point, and per-scene override profiles.
- [ ] Add tonemapping calibration scenes and golden screenshots for ACES, neutral, and custom curves.
- [ ] Add bloom threshold, knee, radius, and mip-chain debug visualization.
- [ ] Expand bloom into a multi-level mip pyramid with threshold/prefilter, downsample, upsample, and composite passes.
- [ ] Add bloom quality controls for mip count, radius, intensity, threshold, soft knee, and firefly suppression.
- [ ] Add dirt, lens, or anamorphic bloom option only if art direction requires it.
- [ ] Add bloom dirt lens texture asset loading, default fallback texture, sampler state, and material/debug binding.
- [ ] Add anamorphic bloom directional blur pass with orientation, stretch, tint, and intensity controls.
- [ ] Add color grading LUT asset loading.
- [ ] Bind color grading LUT in final composite.
- [ ] Add color grading intensity and fallback when LUT is missing.
- [ ] Add real LUT asset pipeline validation for 2D strip and 3D LUT layouts, bit depth, color space, and neutral LUT generation.
- [ ] Add film grain pass.
- [ ] Add vignette pass.
- [ ] Add chromatic aberration pass.
- [ ] Add optional styling toggles for chromatic aberration, vignette, and film grain per post-process profile and per camera.
- [ ] Add sharpen or CAS pass.
- [ ] Add SSAO or GTAO pass before lighting or composite.
- [ ] Add HBAO quality option or document why GTAO replaces it, with comparable radius/intensity controls.
- [ ] Add SSR pass or explicit reflection fallback decision.
- [ ] Add SSPR path for planar/water/floor reflections where SSR is insufficient.
- [ ] Add hybrid reflection composition that blends SSR/SSPR with reflection probes, sky IBL, and material roughness.
- [ ] Add depth of field pass.
- [ ] Add bokeh-aware DOF gather/scatter, focus distance, aperture, near/far blur, and circle-of-confusion debug overlay.
- [ ] Add DOF focus picking or autofocus support.
- [ ] Add motion blur pass.
- [ ] Add camera motion blur from previous/current view-projection matrices with shutter angle and sample count controls.
- [ ] Add object motion blur from per-object previous transforms and skinned/animated motion-vector support.
- [ ] Add camera and object motion vectors for post-process reprojection.
- [ ] Persist previous object transforms in render proxies for object motion vectors, TAA rejection, and motion blur.
- [ ] Add explicit unsupported diagnostics when only camera/depth motion vectors are available.
- [ ] Add jittered projection support in camera matrices and expose per-frame jitter to render views, culling, and reprojection.
- [ ] Add TAA resolve using jittered projection, history color, depth rejection, motion vectors, and neighborhood clamping.
- [ ] Add TAA history validation and reset rules.
- [ ] Add TAA reactive mask or disocclusion rejection.
- [ ] Add TSR or temporal upscaling pass with internal resolution scale, sharpening, reactive mask input, and quality presets.
- [ ] Add upscaling fallback path for non-TSR devices and verify UI/composite scaling.
- [ ] Add post-process quality presets.
- [ ] Add render graph resource declarations for every post-process intermediate.
- [ ] Add post-process debug views for HDR scene color, luminance, exposure, bloom mips, motion vectors, depth, normals, AO, SSR/SSPR, LUT, and final LDR output.
- [ ] Add post-process pass profiling in runtime stats.
- [ ] Add render smoke coverage for HDR exposure, bloom pyramid, tonemap, TAA, and LDR fallback.
- [ ] Add artifact/golden-image coverage for motion vectors, motion blur, DOF, AO, SSR/SSPR, LUT grading, and stylized post-process toggles.

## Geometry / World

- [ ] Add world partitioning or chunk/sector system for large scenes, with streaming-friendly bounds and ownership.
- [ ] Add static mesh streaming by world sector, including load/unload transitions and render-scene synchronization.
- [ ] Add world origin rebasing or floating-origin support for large coordinate spaces.
- [ ] Add spatial acceleration structure for world geometry queries, visibility, probe lookup, decals, and debug selection.
- [ ] Add CPU/GPU occlusion data for world sectors and large static occluders.
- [ ] Add terrain support with heightmap import, material layer masks, LOD, collision handoff, and streaming.
- [ ] Add terrain renderer with geometry clipmaps for camera-relative large terrain.
- [ ] Add terrain mesh LOD fallback for platforms without tessellation or compute-driven terrain.
- [ ] Add terrain tessellation or displacement path where backend capabilities support it.
- [ ] Add terrain virtual texturing or page-table based material streaming for large splat/layer textures.
- [ ] Add terrain normal, height, hole, and material-layer asset import validation.
- [ ] Add terrain debug views for clipmap rings, selected LOD, virtual texture residency, and displacement range.
- [ ] Add instanced foliage/vegetation rendering with GPU culling, LODs, wind parameters, and impostor fallback.
- [ ] Add vegetation instance asset format with per-instance transform, bounds, random seed, species/material id, and LOD data.
- [ ] Add hierarchical vegetation culling by cell/cluster before per-instance culling.
- [ ] Add vegetation wind animation parameters and shader deformation path.
- [ ] Add vegetation impostor asset generation or import path with billboard/hemisphere atlas support.
- [ ] Add vegetation LOD transition strategy with dithering or cross-fade to avoid popping.
- [ ] Add vegetation shadow and depth-only variants for instanced, wind-deformed, and impostor geometry.
- [ ] Add road/path/spline geometry generation or import path if the world pipeline requires authored splines.
- [ ] Add world decal volumes with streaming, culling, atlas/material binding, and editor/debug visualization.
- [ ] Add reflection, GI, and light probe volumes as world entities with sector streaming and lookup acceleration.
- [ ] Add water surface geometry path with reflection/refraction hooks, depth interaction, and LOD.
- [ ] Add sky/atmosphere world component integration with render settings, exposure, and time-of-day data.
- [ ] Add per-sector lighting metadata for static lights, probes, shadow cache invalidation, and fallback budgets.
- [ ] Add async world chunk loading and unloading pipeline decoupled from render submission.
- [ ] Add async GPU asset upload queue for streamed meshes, textures, terrain pages, vegetation cells, and probes.
- [ ] Add residency budgets for streamed world chunks, meshes, textures, virtual texture pages, probes, and foliage instances.
- [ ] Add eviction policy for over-budget streamed assets with pinned and recently-visible resource handling.
- [ ] Add streaming priority model based on camera position, predicted movement, visibility, and gameplay/editor pins.
- [ ] Add visible placeholder or fallback resources for chunks and assets still uploading.
- [ ] Add editor/debug visualization for world sectors, bounds, LOD ranges, occluders, probes, decals, and streaming state.
- [ ] Add world asset budget reporting for geometry memory, textures, instances, probes, and streamed sectors.
- [ ] Add tests for world partition loading/unloading, origin rebasing math, sector bounds, and render-scene sync.
- [ ] Add tests for terrain clipmap LOD selection, virtual texture residency, and terrain fallback path.
- [ ] Add tests for vegetation cell culling, wind shader variant selection, impostor fallback, and LOD transitions.
- [ ] Add tests for streaming priority, budget eviction, async upload completion, and placeholder replacement.

## Skinned Animation Renderer

- [ ] Replace current skinned mesh rejection path with runtime support for skinned vertex formats.
- [ ] Add skeleton, joint, inverse-bind, and animation clip asset loading from glTF.
- [ ] Add CPU animation sampling for joints, clips, blending, masks, and root motion extraction.
- [ ] Add GPU skinning path using bone matrix or dual-quaternion buffers.
- [ ] Add skinned mesh submit path with per-instance skeleton palette binding.
- [ ] Add skinned shadow, depth, velocity, selection, and motion-vector passes.
- [ ] Add morph target asset loading, weights, GPU buffers, and shader deformation path.
- [ ] Add combined skinning plus morph target path with deterministic ordering.
- [ ] Add previous-frame skinned transform storage for TAA and object motion blur.
- [ ] Add cloth simulation/rendering hooks for external simulation buffers or future solver integration.
- [ ] Add hair rendering hooks for strand/card assets, simulation input, shadowing, and material variants.
- [ ] Add animation renderer stats for skinned meshes, joints, palettes, morph targets, upload bytes, and GPU skinning cost.
- [ ] Add tests for glTF skin import, rejection fallback removal, skeleton palette validation, morph targets, and skinned pass routing.

## Performance / Platform

- [ ] Split renderer layering into bgfx backend/RHI wrappers, render core, and scene renderer modules.
- [ ] Add a backend-facing RHI/resource abstraction so higher-level renderer code does not create, destroy, or name raw bgfx handles directly.
- [ ] Add typed RAII wrappers for bgfx textures, buffers, framebuffers, shaders, programs, uniforms, and swap-chain/native-window handles.
- [ ] Add a central deferred GPU destroy queue for every bgfx resource kind, with frame fences and immediate shutdown mode.
- [ ] Add render target and render buffer pools for scene, shadow, post-process, temporal, readback, and editor viewport resources.
- [ ] Add a render command queue boundary between game/editor code and render submission.
- [ ] Add optional render thread execution model with deterministic single-thread fallback.
- [ ] Add frame fences and resource lifetime validation across game, render, and bgfx frame completion.
- [ ] Add upload command batching for static assets, dynamic instance data, material buffers, light lists, and readback requests.
- [ ] Add GPU crash/debug breadcrumb labels for frame, viewport, graph pass, shader program, material variant, and render target.
- [ ] Add renderer validation layer toggles for dev, test, CI, and shipping configurations.
- [ ] Add frame profiler timeline for CPU pass time, render-thread work, GPU pass time, waits, and present cost.
- [ ] Add CPU timing scopes for every render graph pass, scene pass, shadow pass, post-process pass, upload phase, and present phase.
- [ ] Add GPU timing scopes per render graph pass and expose unresolved/unsupported timestamp states cleanly.
- [ ] Correlate CPU and GPU pass timings by frame id, view id, pass name, and render graph node.
- [ ] Add bgfx GPU timestamp integration where supported and explicit unavailable-state reporting where not supported.
- [ ] Add debug marker scopes around every bgfx view/pass, compute dispatch, draw batch category, and resource upload phase.
- [ ] Add RenderDoc-friendly capture labels for frame, viewport, render graph pass, shader program, material variant, and target resource.
- [ ] Add runtime capture trigger integration for RenderDoc or backend capture APIs where available.
- [ ] Add per-pass and per-resource memory stats for render targets, transient graph resources, buffers, textures, and upload staging.
- [ ] Add global render memory budget configuration for transient render targets, static buffers, dynamic buffers, textures, streamed assets, and staging uploads.
- [ ] Add transient buffer allocator for per-frame constants, instance data, draw args, light lists, skinning palettes, and compute scratch data.
- [ ] Add transient buffer lifetime validation and aliasing diagnostics similar to transient texture graph resources.
- [ ] Add texture compression import pipeline for BCn, ETC2, ASTC, and uncompressed fallback targets.
- [ ] Add texture compression format selection per backend, platform tier, alpha mode, normal maps, HDR textures, and quality preset.
- [ ] Add validation for compressed texture mip chains, block alignment, color space, and runtime format support.
- [ ] Add runtime performance HUD or debug panel for frame time, draw calls, triangles, instances, lights, shadows, post-process, streaming, and memory budgets.
- [ ] Add automated performance budgets for frame time, CPU render time, GPU time, draw calls, triangles, upload bytes, memory, shader compile time, and streaming stalls.
- [ ] Fail or warn in CI when performance budgets regress beyond configured tolerance per scene, backend, and quality tier.
- [ ] Add automated performance smoke scenes for many meshes, many lights, heavy post-process, terrain, vegetation, skinning, and streaming.
- [ ] Add benchmark harness output in machine-readable JSON for CI/regression tracking.
- [ ] Add platform capability matrix for D3D11, D3D12, Vulkan, Metal, and headless/noop backends.
- [ ] Add backend quality tiers that map D3D11, D3D12, Vulkan, Metal, and Noop capabilities to concrete renderer feature presets.
- [ ] Add quality-tier override and diagnostics so forced low/medium/high/ultra modes explain every disabled feature.
- [ ] Add feature gating for compute, indirect draws, texture arrays, texture formats, HDR targets, timestamps, tessellation, and readback support.
- [ ] Add backend-specific fallback validation for features that are not portable across all bgfx renderers.
- [ ] Add shader permutation budget reporting and missing-permutation diagnostics per backend profile.
- [ ] Add shader permutation key system for material features, lighting path, shadows, skinning, morph targets, instancing, terrain, vegetation, and post-process variants.
- [ ] Add material compiler that maps material assets and feature flags to validated shader permutations and binding layouts.
- [ ] Add material compiler diagnostics for unsupported feature combinations, missing textures, missing shader variants, and backend capability fallbacks.
- [ ] Add offline shader/material build step that emits permutation manifests for every supported backend profile.
- [ ] Add runtime shader permutation cache with deterministic fallback to safe variants.
- [ ] Add async upload/staging buffer budgeting and stall detection.
- [ ] Add resource lifetime telemetry for cached assets, streamed assets, evictions, and residency misses.
- [ ] Add CPU job/threading model for asset IO, decompression, culling preparation, animation sampling, and streaming decisions.
- [ ] Add synchronization diagnostics for render submission stalls, readbacks, asset uploads, and frame pacing.
- [ ] Add dynamic resolution scaling controller with target frame time, min/max scale, hysteresis, history invalidation, and upscaler integration.
- [ ] Add dynamic quality scaler hooks for resolution, shadows, post-process, GI, vegetation density, terrain LOD, and streaming budgets.
- [ ] Add visual debug view framework with selectable fullscreen views and per-viewport routing.
- [ ] Add visual debug views for world normals, view normals, roughness, metallic, base color, material id, depth, linear depth, motion vectors, light clusters, exposure, bloom mips, overdraw, shadows, AO, SSR, and probe selection.
- [ ] Add overdraw visualization pass for opaque, transparent, vegetation, particles, decals, and editor overlays.
- [ ] Add low-memory and low-end hardware presets with deterministic feature fallback decisions.
- [ ] Add platform tests for renderer initialization, resize/minimize/restore, device loss or reset handling where available, and backend selection.
- [ ] Add CI artifacts for render smoke screenshots, profiler captures, capability reports, and performance summaries.

## Testing / Tooling

- [ ] Make render smoke coverage mandatory in at least one CI/nightly lane instead of only an optional local script flag.
- [ ] Add a CI matrix that separates fast unit tests, GPU smoke, golden images, performance budgets, and shader/material build validation.
- [ ] Add test tags so GPU, slow, golden, performance, and platform-specific render tests can be selected deterministically.
- [ ] Add renderer failure triage artifacts: graph dump, capability report, shader manifest report, runtime stats, screenshot, and relevant logs.
- [ ] Add render graph test fixtures that validate pass ordering, barriers, aliases, resource lifetimes, and debug marker names together.
- [ ] Add profiler tests that verify every enabled render pass emits CPU timing, GPU timing state, memory stats, and stable pass identifiers.
- [ ] Add RenderDoc/debug-marker smoke test or validation hook that checks marker coverage for scene, shadow, post-process, compute, and upload passes.
- [ ] Add dynamic resolution tests for scale changes, history invalidation, viewport resize, UI composition, and low-end fallback.
- [ ] Add backend capability matrix tests for D3D11, D3D12, Vulkan, Metal, and Noop using mocked bgfx caps.
- [ ] Add quality tier snapshot tests that lock feature decisions for low, medium, high, ultra, and custom tiers.
- [ ] Add memory budget tests for transient textures, transient buffers, upload staging, streamed assets, eviction, and over-budget diagnostics.
- [ ] Add texture compression pipeline tests with BCn, ETC2, ASTC, invalid block alignment, missing mips, normal maps, and HDR assets.
- [ ] Add shader permutation tests for key generation, manifest coverage, missing variants, backend fallback, and permutation budget limits.
- [ ] Add material compiler tests for feature flags, binding layouts, texture defaults, unsupported combinations, and deterministic fallback variants.
- [ ] Add golden render scenes for PBR features, IBL/probes, shadows, GI, post-process, terrain, vegetation, skinning, and streaming fallback.
- [ ] Add golden screenshot comparison tests per backend and quality tier with tolerance profiles for D3D11, D3D12, Vulkan, Metal, and Noop/headless substitutes.
- [ ] Store golden baselines with metadata for backend, driver, quality tier, resolution, exposure mode, seed, and asset manifest hash.
- [ ] Add golden screenshot update workflow with reviewable diff artifacts, heatmaps, and failure thumbnails.
- [ ] Add GPU smoke harness presets for each supported backend that can run headless/noop where real GPU capture is unavailable.
- [ ] Add GPU smoke tests for multi-window editor viewports using docked and detached native-window framebuffers in the same frame.
- [ ] Add GPU smoke coverage for detached viewport resize, minimize/restore, close/reopen, swap-chain fallback, and per-viewport temporal history isolation.
- [ ] Add automated performance budget tests that consume profiler JSON and compare against checked-in or CI-provided thresholds.
- [ ] Add shader compile CI for every shader profile, including dxbc, dxil, spirv, spirv16, essl, glsl, and metal outputs.
- [ ] Add shader compile CI diagnostics for compiler version, profile target, defines, include graph, generated binary size, and missing permutation keys.
- [ ] Add visual debug view tests or smoke captures for normals, roughness, metallic, depth, motion vectors, light clusters, exposure, and overdraw.
- [ ] Add CI packaging for shader manifests, material compiler output, texture compression reports, capability reports, profiler JSON, and screenshots.
- [ ] Add developer command or script to regenerate shaders, material permutations, compressed textures, render goldens, and capability snapshots.

## Unreal-grade renderer architecture

- [ ] Define the target renderer layering explicitly: Backend/RHI, RenderCore, Renderer, EditorRenderer, and tools.
- [ ] Move bgfx-specific setup, resource creation, shader loading, and native-window interop behind the Backend/RHI layer.
- [ ] Keep ECS, prefab assets, editor state, and material assets free from direct bgfx handle dependencies.
- [ ] Introduce renderer-owned primitive/light/camera scene proxies with stable ids, dirty flags, version counters, bounds, and cached draw data.
- [ ] Add a compact scene proxy store that supports add/update/remove without rebuilding all render state every frame.
- [ ] Add cached mesh draw commands keyed by mesh, material, section, pass, shader variant, and render state.
- [ ] Invalidate cached draw commands only when relevant mesh, material, transform, visibility, pass, or shader state changes.
- [ ] Add render dependency tracking from ECS components and assets to render proxies, resources, material variants, and pass caches.
- [ ] Add explicit frame phases: extract/sync, graph build, graph compile, upload, execute, readback consume, present, and deferred destroy.
- [ ] Add renderer-owned per-view state for camera matrices, jitter, exposure, temporal history, visibility results, debug mode, and quality tier.
- [ ] Add per-scene renderer state for resource residency, shadow cache, reflection/GI probes, material cache, and draw command cache.
- [ ] Add per-backend capability negotiation before quality-tier selection, with human-readable diagnostics for every downgraded feature.
- [ ] Add RenderDoc/PIX/Metal capture-friendly naming for every resource, view, pass, shader, material variant, and draw category.
- [ ] Add a strict dev validation mode that treats silent fallback, missing graph declarations, ignored material features, and missing resources as errors.
- [ ] Add a shipping validation mode that records diagnostics without aborting, with deterministic fallback resources and feature decisions.
- [ ] Add renderer architecture documentation that maps each production feature to owning module, graph pass, resources, stats, tests, and debug view.

## Engine, asset, and editor render contract

- [ ] Add schema/version metadata for render-facing components, material assets, mesh assets, light components, prefab nodes, and renderer settings.
- [ ] Add migration tests for older prefab/material/light assets when new render fields are introduced.
- [ ] Add round-trip prefab tests for every render-facing component field, including default values and omitted optional fields.
- [ ] Add prefab override/apply/revert tests for every render-facing light, mesh, material, shadow, and post-process property.
- [ ] Add editor inspector controls or explicit read-only diagnostics for every render-facing component property.
- [ ] Add editor warnings when a user selects a feature unsupported by the current backend or quality tier.
- [ ] Add asset import diagnostics that distinguish unsupported, missing, malformed, and intentionally ignored render features.
- [ ] Add material and mesh asset dependency tracking so texture/material/mesh changes invalidate only affected render resources and draw commands.
- [ ] Add hot-reload tests for materials, textures, meshes, shaders, and prefabs while a scene viewport is active.
- [ ] Add deterministic fallback asset policy for missing meshes, textures, materials, shaders, LUTs, probes, and streamed resources.
- [ ] Add a visible editor/debug placeholder path for missing resources that is disabled or configurable for shipping.
- [ ] Add compatibility tests that old scenes/prefabs continue to load with new render component fields and default feature fallbacks.

## Multi-viewport and editor presentation quality

- [ ] Make every editor viewport own independent scene target, post-process targets, temporal history, exposure history, selection mask, and final composite target.
- [ ] Add per-viewport lifecycle tests for dock, undock, redock, close, reopen, resize, minimize, restore, DPI change, and parent-window change.
- [ ] Add multi-viewport render smoke that renders docked and detached viewports with different scenes/cameras/post-process settings in one frame.
- [ ] Add validation that view ids, temporal histories, exposure histories, readbacks, and debug views never cross-contaminate between viewports.
- [ ] Add editor presentation backend that can present bgfx output without GDI blit quality loss where the platform supports it.
- [ ] Add native-window/swap-chain fallback diagnostics when multiple window presentation is unsupported.
- [ ] Add DPI-aware viewport sizing, safe-area preview, and pixel-perfect/fill/fit present validation in GPU smoke.
- [ ] Add screenshot capture per viewport, including detached windows, with metadata for viewport id, backend, resolution, DPI, and quality tier.
- [ ] Add editor viewport capture artifacts to CI for failing GPU smoke and golden tests.

## Backend / RHI foundation

- [ ] Define a renderer backend interface for device initialization, shutdown, frame begin/end, reset, capabilities, and debug labels.
- [ ] Define backend resource interfaces for textures, buffers, samplers, framebuffers, shaders, programs, uniforms, and native-window framebuffers.
- [ ] Wrap bgfx handles in typed renderer handles with owner, generation, debug name, creation flags, size, format, and lifetime metadata.
- [ ] Add backend-independent resource descriptors for textures, buffers, framebuffers, samplers, shader programs, and readback requests.
- [ ] Add resource creation validation before calling bgfx, including dimensions, mip counts, array layers, format support, flags, and usage.
- [ ] Add resource destruction validation so handles are destroyed once and only after all frame references are retired.
- [ ] Add backend frame fences or frame-number retirement for deferred resource destruction.
- [ ] Add a central GPU resource registry that owns every runtime bgfx resource outside third-party internals.
- [ ] Add a central transient resource allocator for graph-owned textures and buffers.
- [ ] Add persistent resource pools for render targets, shadow maps, readback targets, temporal history, and editor viewport framebuffers.
- [ ] Add upload heap/staging abstractions for static asset upload, dynamic data upload, and readback staging.
- [ ] Add typed dynamic buffer allocators for per-frame constants, instances, lights, draw arguments, skinning palettes, particles, and compute scratch.
- [ ] Add alignment, stride, and lifetime validation for dynamic buffers and transient allocations.
- [ ] Add explicit backend support queries for every texture format, buffer usage, compute path, indirect draw path, timestamp path, and readback path.
- [ ] Add backend reset/device-loss handling for default framebuffer, native-window framebuffers, render targets, shaders, uniforms, and cached state.
- [ ] Add device-lost diagnostics and recovery tests for platforms where bgfx exposes reset or invalidation behavior.
- [ ] Add backend error callback routing into renderer diagnostics with severity, frame id, pass name, resource name, and backend message.
- [ ] Add backend debug marker API for frame, pass, draw, dispatch, upload, resource allocation, and readback scopes.
- [ ] Add backend capture integration points for RenderDoc, PIX, Xcode/Metal, and bgfx captures where available.
- [ ] Add backend-independent screenshot/readback API with async completion, row pitch metadata, color space metadata, and failure diagnostics.
- [ ] Add backend memory telemetry for static buffers, dynamic buffers, textures, framebuffers, transient allocations, staging uploads, and readbacks.
- [ ] Add backend shutdown leak reporting for every live resource with creation site, owner, size, and last-used frame.

## Render graph execution

- [ ] Add render graph pass execution callbacks that receive typed resources and pass parameters instead of manually reading renderer globals.
- [ ] Add graph executor scheduling that applies compiled pass order, barriers, view ids, debug labels, and resource bindings consistently.
- [ ] Add graph-side external resource registration for backbuffer, native-window outputs, scene targets, imported textures, and persistent histories.
- [ ] Add graph-side transient texture creation from compiled lifetimes and alias slots.
- [ ] Add graph-side transient buffer creation from compiled lifetimes and alias slots.
- [ ] Add graph-side resource clear operations with explicit color, depth, stencil, and load/store policy.
- [ ] Add graph-side load/store metadata for color, depth, stencil, resolve, discard, and preserve operations.
- [ ] Add graph-side framebuffer creation and reuse for every render target attachment set.
- [ ] Add graph-side pass culling that preserves required debug/touch/clear passes and culls unused optional passes.
- [ ] Add graph-side subresource tracking for mip levels, array slices, cube faces, and temporal ping-pong resources.
- [ ] Add graph support for compute passes with read/write buffers, read/write images, dispatch dimensions, and capability gating.
- [ ] Add graph support for copy/blit/readback passes with explicit source/destination state transitions.
- [ ] Add graph support for async readback requests with frame latency and non-stalling result consumption.
- [ ] Add graph validation for resource format compatibility across producers and consumers.
- [ ] Add graph validation for pass attachment compatibility: color/depth formats, sample count, dimensions, and array layers.
- [ ] Add graph validation for output color space transitions: scene-linear HDR, post-HDR, display-linear, display-encoded LDR.
- [ ] Add graph validation for temporal resources that must persist across frames and must not alias transients.
- [ ] Add graph validation for multi-viewport view id ranges and detached viewport stride overflow.
- [ ] Add graph validation for pass names, debug marker names, resource names, and stable ids.
- [ ] Add graph replay/debug dump that can reconstruct frame pass order and resource states from CI artifacts.
- [ ] Add render graph unit tests for external resources, persistent history resources, transient aliasing, compute passes, copy passes, and readback passes.
- [ ] Add render graph fuzz tests for invalid pass/resource declarations and deterministic diagnostics.

## Scene renderer and visibility architecture

- [ ] Add renderer-owned scene proxy ids independent from ECS entity ids, with stable mapping and stale-id validation.
- [ ] Add primitive scene proxies for mesh renderers with world bounds, local bounds, material slots, visibility, shadow flags, motion data, and owner scene id.
- [ ] Add light scene proxies with type, color, intensity, range, cone angles, area shape, shadow flags, volumetric flags, and versioning.
- [ ] Add camera/view proxies with projection, view, jitter, exposure settings, post-process profile, near/far policy, and camera-cut markers.
- [ ] Add environment/sky/probe proxies with resource handles, bounds, priority, intensity, and fallback policy.
- [ ] Add decal proxies with volume transform, projection mode, material, blend mode, layer mask, priority, and culling bounds.
- [ ] Add particle/VFX proxies with bounds, simulation state, material variants, sort keys, and GPU buffer handles.
- [ ] Add terrain proxies with clipmap state, sector bounds, material layers, virtual texture state, and LOD ranges.
- [ ] Add vegetation proxies with cell bounds, species/material ids, wind parameters, LOD data, and impostor metadata.
- [ ] Add skinned mesh proxies with skeleton palette, previous pose, morph weights, material slots, and pass eligibility.
- [ ] Add proxy dirty flags for transform, visibility, mesh, material, lighting, shadow, animation, bounds, and render-state changes.
- [ ] Add incremental scene sync that only updates changed proxies and removes dead proxies deterministically.
- [ ] Add proxy compaction or sparse storage policy that avoids invalidating stable ids unexpectedly.
- [ ] Add broad-phase spatial structure for primitive, light, decal, probe, terrain, vegetation, and debug-picking queries.
- [ ] Add CPU frustum culling per view using scene proxy bounds and per-pass visibility flags.
- [ ] Add occlusion culling state per view with frame-lagged results and conservative fallback.
- [ ] Add draw command cache keyed by pass, mesh, section, material, shader variant, render state, and vertex/index layout.
- [ ] Add draw command invalidation when any dependent resource, proxy flag, material variant, or shader permutation changes.
- [ ] Add pass-specific visibility masks for base pass, depth prepass, shadow, selection, velocity, translucency, decals, and debug views.
- [ ] Add per-view visibility results with visible primitive count, culled primitive count, occluded primitive count, and skipped reason counters.
- [ ] Add deterministic sorting for opaque, masked, transparent, decals, particles, editor overlays, and debug draws.
- [ ] Add scene renderer tests for proxy lifecycle, dirty propagation, visibility masks, culling, sorting, and command cache invalidation.

## Mesh, geometry, and asset pipeline

- [ ] Add mesh asset version metadata with vertex streams, index streams, sections, material slots, bounds, LODs, meshlets, and collision handoff.
- [ ] Add mesh import validation for positions, normals, tangents, UVs, colors, joints, weights, morph targets, and index ranges.
- [ ] Generate missing tangents with a deterministic MikkTSpace-compatible path or explicit unsupported diagnostics.
- [ ] Generate mesh bounds per section, per LOD, per meshlet, and per asset with validation against invalid or NaN data.
- [ ] Add mesh optimization pipeline: vertex cache optimization, overdraw optimization, vertex fetch optimization, and index remapping.
- [ ] Add meshlet generation pipeline with cone data, bounds, LOD association, and backend limits.
- [ ] Add LOD group asset data with screen-size thresholds, hysteresis, forced LOD, and editor preview override.
- [ ] Add CPU LOD selection tests for distance, screen coverage, hysteresis, forced LOD, and missing LOD fallback.
- [ ] Add GPU LOD input buffer generation and validation against CPU LOD selection.
- [ ] Add static mesh streaming chunks for vertex/index buffers, meshlets, materials, and fallback low LODs.
- [ ] Add placeholder mesh policy for missing, streaming, invalid, or unsupported geometry.
- [ ] Add mesh resource residency tracking, pinned state, last-used frame, memory size, and eviction policy.
- [ ] Add async mesh load and GPU upload path with completion fences and render-scene synchronization.
- [ ] Add mesh hot-reload that updates resource handles, draw command caches, bounds, and material slots safely.
- [ ] Add mesh asset tests for malformed files, invalid sections, invalid material slots, missing tangents, huge meshes, and streaming chunks.
- [ ] Add mesh golden scenes for UV seams, tangent-space normals, alpha masking, LOD transitions, instancing, and shadow casting.

## Texture and image pipeline

- [ ] Add texture asset metadata for dimensions, format, mip count, array layers, cube faces, color space, compression, and intended usage.
- [ ] Add texture import validation for dimensions, block alignment, mip completeness, color space, HDR ranges, and normal-map encoding.
- [ ] Add backend texture format selection for BCn, ETC2, ASTC, RGBA8, RGBA16F, R11G11B10F, R16F, R32F, depth, and stencil formats.
- [ ] Add texture fallback policy per usage: white, black, flat normal, roughness, metallic, AO, emissive, LUT, shadow, probe, and missing debug.
- [ ] Add texture streaming with mip residency, upload queue, eviction, pinned mips, visible-priority requests, and budget diagnostics.
- [ ] Add virtual texture groundwork for terrain/material layers with page table, feedback, residency, and fallback pages.
- [ ] Add cubemap import and validation for skyboxes, irradiance, prefiltered radiance, reflection probes, and point-light shadows.
- [ ] Add 3D texture and LUT import validation for color grading, volumetrics, noise, and simulation data.
- [ ] Add sRGB/linear sampling validation for albedo, emissive, normal, roughness, metallic, AO, masks, LUTs, and HDR textures.
- [ ] Add texture hot-reload with dependency invalidation for materials, probes, LUTs, skyboxes, and post-process profiles.
- [ ] Add async texture upload tests, streaming residency tests, compression tests, fallback tests, and color-space tests.

## Shader system and material compiler

- [ ] Add shader source registry with stable shader ids, stages, entry points, include graph, defines, backend profiles, and debug names.
- [ ] Add shader permutation key schema for material features, mesh features, lighting path, shadow mode, skinning, morphing, instancing, terrain, vegetation, decals, and post-process.
- [ ] Add shader compile manifest that records every required permutation per backend and quality tier.
- [ ] Add offline shader build step that emits binaries, reflection metadata, binding layouts, permutation manifests, and diagnostics.
- [ ] Add runtime shader lookup by permutation key with deterministic fallback to safe variants and explicit missing-permutation diagnostics.
- [ ] Add shader reflection data for uniforms, samplers, storage buffers, images, vertex attributes, and render target formats.
- [ ] Add material compiler that maps asset properties and textures to shader feature bits, binding layouts, default resources, and validation messages.
- [ ] Add material variant cache keyed by material asset id, shader feature set, backend, quality tier, and pass type.
- [ ] Add material binding layout validation for every shader permutation and every material feature combination.
- [ ] Add unsupported material feature policy: hard error, editor warning, runtime fallback, or disabled feature with stats.
- [ ] Add shader hot-reload in editor with permutation cache invalidation and safe fallback while recompiling.
- [ ] Add shader compile budget metrics for permutation count, binary size, compile time, reflection size, and runtime cache size.
- [ ] Add shader tests for permutation key stability, manifest coverage, fallback selection, binding layout compatibility, and missing shader diagnostics.
- [ ] Add material compiler tests for every advanced PBR feature, texture default, unsupported feature combination, and backend fallback.

## Render passes and frame pipeline completeness

- [ ] Add depth prepass with masked/opaque policy, depth format validation, and fallback when depth prepass is disabled.
- [ ] Add normal/roughness/material-id prepass or G-buffer path for deferred, SSAO/GTAO, SSR, and debug views.
- [ ] Add base opaque pass with physically based lighting, shadows, IBL, decals, lightmaps, AO, and material variants.
- [ ] Add masked pass with alpha cutoff, dithering, shadow/depth variants, and deterministic sorting.
- [ ] Add transparent pass with back-to-front sorting, weighted blended OIT option, refraction hooks, and depth-aware composition.
- [ ] Add velocity pass for camera, rigid object, skinned mesh, morph target, vegetation wind, and particles.
- [ ] Add selection/id pass for editor picking, outline mask, and hit proxy diagnostics.
- [ ] Add editor overlay pass for grid, gizmos, bounds, icons, debug text, and selection highlights.
- [ ] Add final composite pass with output transform, gamma, tonemap, UI-safe composition, and display format validation.
- [ ] Add debug-view pass routing that can replace or overlay final output per viewport.
- [ ] Add pass enable/disable dependency validation so disabled passes cannot leave dangling graph reads.
- [ ] Add pass-level stats for draw calls, dispatches, triangles, instances, resources read/written, timing, and memory footprint.
- [ ] Add pass-level error diagnostics with pass name, graph node id, resource id, backend view id, and failure reason.

## Translucency, particles, and VFX

- [ ] Add transparent material classification for alpha blend, additive, multiplicative, premultiplied, refractive, and distortion modes.
- [ ] Add transparent sorting keys by layer, material priority, distance, depth bucket, and stable tie-breaker.
- [ ] Add weighted blended OIT path with accumulation and revealage targets.
- [ ] Add fallback sorted alpha blending path for devices without required OIT support.
- [ ] Add refraction pass that samples scene color/depth with roughness and thickness support.
- [ ] Add soft particles with depth fade, camera fade, near-plane fade, and motion vectors where possible.
- [ ] Add GPU particle buffer layout for positions, velocities, colors, sizes, rotations, lifetimes, and sort keys.
- [ ] Add CPU fallback particle simulation and submit path for low-end devices.
- [ ] Add GPU particle simulation dispatch with dead/alive lists, emit counters, and indirect draw support.
- [ ] Add particle sorting for transparent particles with CPU fallback and GPU sort path.
- [ ] Add ribbon/trail rendering path with camera-facing and velocity-facing modes.
- [ ] Add beam rendering path with noise, tapering, UV scrolling, and depth fade.
- [ ] Add sprite sheet animation, flipbook blending, and texture atlas validation.
- [ ] Add VFX budget stats for emitters, particles, draw calls, simulation time, GPU memory, and dropped particles.
- [ ] Add render smoke scenes for alpha blend, additive, refractive, soft particles, ribbons, beams, and GPU/CPU fallback.

## Decals and surface projection

- [ ] Add decal component and render proxy with transform, bounds, material, blend mode, projection depth, normal threshold, and sort order.
- [ ] Add mesh decals or projected decals path depending on available G-buffer/depth data.
- [ ] Add deferred decal path for base color, normal, roughness, metallic, AO, emissive, and mask channels.
- [ ] Add forward decal fallback for platforms or paths without deferred/G-buffer targets.
- [ ] Add decal atlas/material binding validation and fallback textures.
- [ ] Add decal culling against view frustum, receiver bounds, and decal volume.
- [ ] Add decal layering and priority conflict resolution with deterministic order.
- [ ] Add decal debug view for bounds, affected receivers, atlas page, blend mode, and overdraw.
- [ ] Add decal tests for projection math, culling, sorting, missing material fallback, and feature gating.

## Cameras, views, and render settings

- [ ] Add render view descriptor that owns camera matrices, viewport rect, scissor, jitter, exposure, post-process profile, quality tier, and debug view.
- [ ] Add camera component fields for exposure mode, post-process profile, FOV, near/far, projection mode, jitter enable, and camera cut id.
- [ ] Add orthographic camera support in scene, shadows, culling, post-process reprojection, and editor views.
- [ ] Add reverse-Z and depth range policy validation per backend.
- [ ] Add view-family concept for multiple views sharing scene data, shadows, probes, and render graph resources.
- [ ] Add stereo/VR-ready view descriptors even if implementation remains disabled.
- [ ] Add split-screen/multi-camera render submission tests.
- [ ] Add per-camera render settings overrides for shadows, post-process, resolution scale, debug view, and quality tier.
- [ ] Add camera cut detection and explicit camera cut API for gameplay/editor callers.
- [ ] Add render settings assets/profiles with versioning, validation, fallback, and editor inspection.
- [ ] Add tests for perspective, orthographic, resize, camera cuts, jitter sequences, and per-camera overrides.

## Render debugging and visualization

- [ ] Add in-app render debug menu with per-viewport view selection and capture/export controls.
- [ ] Add debug view framework with stable ids, names, required resources, and fallback if resource is unavailable.
- [ ] Add debug view for base color, metallic, roughness, normal, tangent, world position, depth, linear depth, stencil, material id, object id, motion vectors, velocity magnitude, exposure, luminance, bloom mips, shadow maps, shadow cascades, light clusters, probes, decals, AO, SSR, GI, overdraw, wireframe, bounds, LOD, meshlets, and streaming residency.
- [ ] Add debug overlays for draw call count, pass timings, render target memory, upload bytes, resource budgets, and feature fallbacks.
- [ ] Add selectable object debug panel showing render proxy id, mesh, materials, shader variant, bounds, LOD, visibility, lights, shadows, and resource residency.
- [ ] Add render graph debug viewer with pass order, resources, aliases, barriers, lifetimes, and pass timings.
- [ ] Add shader/material debug viewer with active permutation, defines, textures, uniforms, and unsupported feature diagnostics.
- [ ] Add live capability report panel with backend, limits, supported features, quality tier, disabled features, and fallback reasons.
- [ ] Add frame capture button that exports graph dump, stats, screenshots, capability report, and relevant logs.
- [ ] Add debug visualization tests or smoke screenshots for every debug view category.

## Quality tiers, scalability, and runtime settings

- [ ] Define low, medium, high, ultra, cinematic, editor, and custom quality tiers.
- [ ] Map every renderer feature to quality tier defaults, backend requirements, memory budget, and fallback policy.
- [ ] Add feature override system that explains why requested features are enabled, disabled, or downgraded.
- [ ] Add dynamic resolution policy with min/max scale, target frame time, hysteresis, camera-cut/history invalidation, and upscaler integration.
- [ ] Add dynamic quality scaler hooks for shadows, GI, AO, SSR, bloom, translucency, particles, vegetation, terrain, texture streaming, and resolution.
- [ ] Add quality preset serialization and editor controls with validation and backend-specific diagnostics.
- [ ] Add runtime stats for active tier, active overrides, disabled features, fallback reasons, and dynamic scaler decisions.
- [ ] Add tests for tier selection, backend fallback, forced overrides, dynamic scaling, history invalidation, and deterministic diagnostics.

## Memory, residency, and streaming

- [ ] Add global render memory budget with separate pools for render targets, buffers, textures, shaders, materials, streamed assets, and staging.
- [ ] Add per-scene and per-world render memory budgets with editor overrides.
- [ ] Add resource residency tracking with last-used frame, visible priority, pinned state, streaming state, and owner scene.
- [ ] Add eviction policy for textures, meshes, probes, terrain pages, vegetation cells, material variants, and shader permutations.
- [ ] Add over-budget diagnostics with responsible resources, eviction candidates, pinned resources, and fallback actions.
- [ ] Add async IO/decode/upload queues for textures, meshes, materials, shaders, probes, lightmaps, terrain, vegetation, and animation data.
- [ ] Add upload throttling and stall detection for frame-time budget compliance.
- [ ] Add placeholder replacement path when streamed resources finish uploading.
- [ ] Add resource dependency graph so evicting a texture/material/mesh invalidates dependent draw commands safely.
- [ ] Add tests for budget enforcement, eviction order, pinned resources, async upload completion, placeholder replacement, and scene release cleanup.

## Platform, packaging, and deployment

- [ ] Add platform capability snapshots for D3D11, D3D12, Vulkan, Metal, OpenGL, OpenGLES, and Noop/headless.
- [ ] Add platform-specific shader profile packaging and runtime lookup validation.
- [ ] Add packaging validation that required shaders, textures, materials, LUTs, probes, default assets, and metadata are present.
- [ ] Add runtime startup diagnostics for missing packaged assets, missing shader profiles, unsupported backend, and fallback backend selection.
- [ ] Add renderer config file support for preferred backend, quality tier, debug validation, shader path, asset path, and capture settings.
- [ ] Add deterministic backend selection policy for editor, runtime, CI, and headless tests.
- [ ] Add support policy documentation for each backend and feature tier.
- [ ] Add automated packaging tests for runtime executable, editor executable, shaders, default assets, and smoke scenes.
- [ ] Add CI artifacts for packaged runtime smoke and editor smoke per supported Windows backend.

## Release gates and acceptance criteria

- [ ] Define production readiness gates for renderer architecture, render graph, resources, materials, lighting, shadows, post-process, editor viewports, tests, performance, and tooling.
- [ ] Require every renderer feature to declare owner module, capability requirements, fallback behavior, runtime stats, debug view, tests, and smoke scene before being marked complete.
- [ ] Require every new render pass to declare graph resources, debug marker, CPU timing, GPU timing state, memory stats, feature gate, and tests.
- [ ] Require every new shader/material feature to declare permutation keys, binding layout, default resources, unsupported diagnostics, asset tests, and render smoke.
- [ ] Require every new render resource type to declare lifetime owner, deferred destroy behavior, memory accounting, debug name, and leak tests.
- [ ] Require every new editor viewport/render feature to include lifecycle tests and at least one multi-viewport smoke or screenshot artifact.
- [ ] Require every backend fallback to be visible in capability reports, runtime stats, logs, and debug UI.
- [ ] Require every screenshot/golden test to store backend, driver, resolution, quality tier, seed, exposure mode, and asset manifest hash.
- [ ] Add release checklist that blocks shipping on missing shader manifests, failing GPU smoke, missing default assets, resource leaks, graph validation errors, and untriaged fallbacks.

## Priorytet: GPU HDR luminance histogram i temporal auto-exposure

- [ ] Make GPU HDR luminance histogram the default auto-exposure path when compute, HDR target sampling, and readback/storage support are available.
- [ ] Keep the current scene-lighting estimator only as a low-end fallback, with explicit stats and diagnostics when it is used.
- [ ] Replace the current fragment/readback exposure pass with a compute histogram path that reads the HDR scene color target directly.
- [ ] Add histogram clear, luminance accumulation, and reduction passes with render graph resources, barriers, view ids, and debug markers.
- [ ] Store histogram bins in a GPU buffer or texture format that preserves enough precision for bright HDR scenes.
- [ ] Add percentile clipping controls for low/high luminance outliers before computing metered average luminance.
- [ ] Add exposure buffer output with metered luminance, adapted luminance, exposure stops, min/max EV, and debug counters.
- [ ] Move temporal adaptation to the GPU when supported, using previous exposure history and delta time.
- [ ] Keep CPU temporal adaptation as fallback for devices without compute/readback support.
- [ ] Reset exposure history on camera cuts, viewport resize, HDR target recreation, quality-tier changes, and explicit post-process profile changes.
- [ ] Feed adapted luminance into tonemap/final composite without waiting on same-frame CPU readback.
- [ ] Add one-frame-latency path for debug readback so profiling and UI stats do not stall rendering.
- [ ] Add exposure debug view showing HDR luminance, histogram bins, clipped percentile range, metered EV, adapted EV, and final exposure stops.
- [ ] Add tests for histogram binning, percentile clipping, adaptation rates, reset rules, fallback selection, and render graph resource declarations.
- [ ] Add render smoke scene that switches between dark interior, bright exterior, and high-contrast highlights to validate temporal exposure behavior.

## Najblizsze najwazniejsze kroki dla najwyzszej jakosci

- [ ] Implement GPU HDR luminance histogram and temporal auto-exposure first to close the real HDR/post-process pipeline.
- [ ] Stabilize renderer capability matrix and backend quality tiers before adding more backend-specific features.
- [ ] Build the render graph/resource lifetime foundation first: barriers, aliases, transient buffers, pass markers, and per-pass profiling.
- [ ] Add shader permutation system and material compiler before expanding advanced PBR, terrain, vegetation, and skinning variants.
- [ ] Add visual debug views early so every new renderer feature can be inspected without RenderDoc.
- [ ] Add golden screenshots and GPU smoke harness per backend/quality tier before tuning lighting and post-process.
- [ ] Add performance budgets and profiler JSON before shipping large systems such as terrain, clustered lighting, GI, and streaming.
- [ ] Implement dynamic resolution and quality scaler after profiler metrics are trustworthy.
- [ ] Expand production features in this order: lighting/shadows, IBL/probes, post-process/HDR, terrain/vegetation, skinning/animation, GI.
