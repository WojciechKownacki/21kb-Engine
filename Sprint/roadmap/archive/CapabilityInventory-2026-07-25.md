# Archived Capability Inventory — 2026-07-25

> Classification: `NON-EXECUTABLE`
>
> This immutable archive preserves the source feature inventory from which the canonical sprint files were created. The active task lists live in [`Sprint/README.md`](../../README.md) and `Sprint/sprints/`. Entries here are not implementation instructions. Agents must not edit this archive or implement an entry from it directly.

The inventory intentionally retains broad and overlapping ideas so they can be evaluated without losing product intent. Duplicate entries, obsolete assumptions, already implemented capabilities, and optional product features are expected until baseline reconciliation assigns each candidate one of these dispositions:

- `IMPLEMENTED`: the repository already satisfies the intended capability;
- `PARTIAL`: a verified implementation exists but material gaps remain;
- `CANDIDATE`: the capability is not yet approved for implementation;
- `REJECTED`: the capability conflicts with product scope, architecture, measured need, licensing, security, or maintenance constraints;
- `PROMOTED`: the capability has one or more canonical work-item specifications in the sprint manifest.

# Capability 01 · Foundation / Core

**Objective:** Establish the shared runtime foundation, contracts, platform abstractions, diagnostics, memory model, concurrency primitives, serialization, configuration, reflection, and module boundaries required by every later engine sprint.

## Core / Foundation

- Add a shared `core` base module every subsystem depends on
- Add an open-addressing hash map and hash set (swiss / robin-hood)
- Add a small-buffer-optimized vector (SmallVector / InlineVector)
- Add fixed-capacity and stack-backed array/vector types
- Add intrusive linked list and intrusive hash map
- Add a custom string type with small-string optimization
- Add an interned string / string-ID table with reverse lookup
- Add a shared hashing header (FNV-1a, xxHash, hash_combine, byte hashing)
- Replace ad-hoc per-file hashing with the shared hashing header
- Add a compile-time stable `TypeId<T>()` facility
- Add a generic generational handle / slot-map template
- Add a generic `Result<T, E>` / `Expected` and error-category system
- Add engine-wide `Span` and `StringView` types
- Add bit utilities (bitset, enum flags, popcount/clz wrappers)
- Add UUID / GUID generation and parsing
- Add a tagged-variant helper with visitation

## Logging / Diagnostics / Assertions

- Add an engine-wide logging facility with levels and categories
- Add pluggable log sinks (console, file, editor console, debugger)
- Add async logging with rotation and unified formatting
- Add structured logging with key/value fields
- Add per-category runtime verbosity thresholds
- Add assertion macros (assert/check/ensure/verify) that are build-config aware
- Add a fatal-error handler with message and safe shutdown
- Add stack-trace capture with symbol resolution
- Add crash minidump writing for runtime, CLI, and hub
- Add a cross-platform crash handler
- Add a runtime in-game console with command entry
- Route all subsystem diagnostics through the unified logger

## Memory Management

- Add a linear / arena (bump) allocator
- Add a stack allocator with scoped markers
- Add a pool / block allocator for fixed-size objects
- Add a per-frame double-buffered frame allocator
- Add a general heap allocator wrapper with alignment support
- Add alignment helpers (AlignUp/AlignDown, aligned alloc/free)
- Add a memory tracker with tagged allocation categories
- Add per-subsystem memory budgets with over-budget diagnostics
- Add allocation statistics and telemetry (peak, live, count per tag)
- Add leak detection for debug builds
- Add STL allocator adapters that route through the tracker
- Add an intrusive ref-counted base and custom smart pointers
- Add a slot-map allocator with generational handle safety
- Route third-party allocators through the memory tracker

## Math / Geometry

- Add SIMD-accelerated Vec3/Vec4/Quat/Mat4 with scalar fallback
- Add a Transform type with compose, inverse, and interpolation
- Add Mat4 general inverse, TRS decompose, and orthonormalize
- Add perspective, orthographic, lookAt, and frustum matrix builders
- Add Sphere, OBB, Frustum, Capsule, Triangle, Segment, and Circle primitives
- Add ray intersection tests: AABB, Sphere, Triangle, OBB
- Add volume overlap tests: AABB-AABB, Sphere-Sphere, Frustum-AABB, Frustum-Sphere
- Add closest-point and distance queries (point-segment, point-AABB, segment-segment)
- Add spline types: Bezier, Catmull-Rom, Hermite, B-spline
- Add arc-length parameterization and spline path sampling
- Add an optional double-precision math variant for large worlds
- Add half-float (float16) conversion helpers
- Add swizzles and component-wise ops across Vec2/Vec3/Vec4
- Move duplicated renderer/editor matrix helpers onto the shared math library
- Move frustum/sphere culling primitives into the shared math library

## CPU / Threading / Job System

- Extract a general-purpose job system decoupled from ECS
- Add a lock-free work-stealing deque (Chase-Lev)
- Add fiber-based job execution with context switching
- Add job priorities and per-job continuations at the execution layer
- Add lock-free MPMC and SPSC queues
- Add a lock-free ring buffer
- Add a main-thread / render-thread dispatch queue
- Add an async task/future framework (task, when_all, when_any)
- Add parallel-for over arbitrary ranges outside ECS chunks
- Add an async I/O framework for asset and file loading
- Add thread naming and per-thread scratch context
- Implement the AVX2 and AVX-512 kernel backends
- Add a general SIMD kernel library reusable outside ECS transforms
- Add a cross-subsystem task-barrier abstraction

## Platform Abstraction Layer

- Add a central platform (HAL) module abstracting OS specifics
- Add a windowing abstraction with pluggable backends
- Add Linux (X11 / Wayland) window and input backends
- Add macOS window and input backends
- Add Android window, input, and lifecycle backends
- Add a virtual filesystem with mount points and archive/pak support
- Add async file I/O to the virtual filesystem
- Add a high-resolution clock and frame-timer abstraction
- Add an environment/paths service (exe, user data, config, cache, temp)
- Add a file/directory watcher (ReadDirectoryChanges / inotify / FSEvents)
- Add a subprocess spawning and pipe abstraction
- Add a CPU/platform info service (cores, cache line, page size, features)
- Promote dynamic library loading out of the module loader into the HAL
- Add a thread wrapper with naming, priority, and affinity
- Add power/battery and display-info queries for scalability

## Reflection / Type System / Metadata

- Add a global type registry for arbitrary C++ types, not just ECS components
- Add content-stable type IDs independent of registration order
- Add enum reflection with name-value tables
- Add reflection for nested structs and composite fields
- Add string, array, map, and handle/reference field types to reflection
- Add method/function reflection for native types
- Add attribute metadata (range, tooltip, category, transient, hidden)
- Add a codegen path to remove hand-written field registration
- Drive the editor inspector UI automatically from reflection metadata
- Drive serialization automatically from reflection metadata
- Add reflection-based property binding for scripting
- Add default-value and validation metadata per field

## Serialization

- Add a unified archive abstraction shared by all subsystems
- Add a JSON serialization backend for data and settings
- Add a diff-friendly, hand-editable text scene/prefab/resource format
- Add endian-portable binary serialization across all codecs
- Add schema evolution (tolerant add/remove/rename of fields)
- Extend the declarative migration framework beyond save games to all formats
- Add object-graph serialization with pointer/reference fixup
- Add cross-asset and cross-entity reference field serialization
- Extend the value model to strings, arrays, maps, nested structs, and handles
- Add reflection-driven automatic serialization for reflected types
- Add versioned per-component schema headers with upgrade hooks
- Add binary-to-text round-trip conversion for debugging

## Config / CVars / Settings

- Add a typed CVar registry of named runtime variables
- Add CVar get/set through the runtime console
- Add CVar overrides from command line, environment, and config file
- Add a hand-editable, diffable text config format (engine, user, project)
- Add layered settings tiers (engine, project, user, platform, command line)
- Add per-platform config overrides
- Add hot-reload of settings via the file watcher
- Add a unified command-line parser shared by all executables
- Add a `-set key=value` override pipeline into CVars/settings
- Add settings schema, validation, and migration for text config
- Auto-build the settings UI from reflection metadata
- Add user-preferences and editor-layout persistence

## Module / Plugin System

- Add external plugin manifest files (name, version, dependencies, content)
- Add plugin discovery/scan of plugin directories
- Add semver version constraints and compatibility checking
- Add optional vs required dependencies with failure isolation
- Add automatic hot-reload driven by a filesystem watcher
- Add an inter-module typed service/interface registry
- Harden the plugin ABI to a versioned struct-of-function-pointers boundary
- Add per-plugin content/asset mounting through the virtual filesystem
- Add an editor "reload plugin" action with state preservation
- Add plugin load-failure sandboxing so one plugin cannot crash the host

# Capability 02 · GPU

**Objective:** Deliver a production renderer foundation with explicit resource ownership, a validated render graph, scalable materials and lighting, predictable backend behavior, editor integration, diagnostics, automated visual verification, and measurable performance budgets.

## Backend / RHI foundation

- Define a renderer backend interface for device initialization, shutdown, frame begin/end, reset, capabilities, and debug labels.
- Define backend resource interfaces for textures, buffers, samplers, framebuffers, shaders, programs, uniforms, and native-window framebuffers.
- Wrap bgfx handles in typed renderer handles with owner, generation, debug name, creation flags, size, format, and lifetime metadata.
- Add backend-independent resource descriptors for textures, buffers, framebuffers, samplers, shader programs, and readback requests.
- Add resource creation validation before calling bgfx, including dimensions, mip counts, array layers, format support, flags, and usage.
- Add resource destruction validation so handles are destroyed once and only after all frame references are retired.
- Add backend frame fences or frame-number retirement for deferred resource destruction.
- Add a central GPU resource registry that owns every runtime bgfx resource outside third-party internals.
- Add a central transient resource allocator for graph-owned textures and buffers.
- Add persistent resource pools for render targets, shadow maps, readback targets, temporal history, and editor viewport framebuffers.
- Add upload heap/staging abstractions for static asset upload, dynamic data upload, and readback staging.
- Add typed dynamic buffer allocators for per-frame constants, instances, lights, draw arguments, skinning palettes, particles, and compute scratch.
- Add alignment, stride, and lifetime validation for dynamic buffers and transient allocations.
- Add explicit backend support queries for every texture format, buffer usage, compute path, indirect draw path, timestamp path, and readback path.
- Add backend reset/device-loss handling for default framebuffer, native-window framebuffers, render targets, shaders, uniforms, and cached state.
- Add device-lost diagnostics and recovery tests for platforms where bgfx exposes reset or invalidation behavior.
- Add backend error callback routing into renderer diagnostics with severity, frame id, pass name, resource name, and backend message.
- Add backend debug marker API for frame, pass, draw, dispatch, upload, resource allocation, and readback scopes.
- Add backend capture integration points for RenderDoc, PIX, Xcode/Metal, and bgfx captures where available.
- Add backend-independent screenshot/readback API with async completion, row pitch metadata, color space metadata, and failure diagnostics.
- Add backend memory telemetry for static buffers, dynamic buffers, textures, framebuffers, transient allocations, staging uploads, and readbacks.
- Add backend shutdown leak reporting for every live resource with creation site, owner, size, and last-used frame.

## Robust render graph

- Split render graph building, graph compilation, and graph execution into explicit stages.
- Make the render graph executor own bgfx view order generation instead of patching view order manually after graph compile.
- Add typed pass parameter structs so every pass binds inputs, outputs, uniforms, samplers, and debug names through one validated contract.
- Add a render graph blackboard for per-frame view, lighting, temporal, exposure, and editor data shared between passes.
- Ensure every frame resource is declared through the render graph.
- Ensure every pass declares all read and write resources.
- Declare dynamic bloom mip resources and views as graph resources/passes instead of appending mip view ids outside the graph.
- Declare per-viewport temporal history read/write textures through the graph with explicit external/history lifetimes.
- Declare exposure histogram/readback resources through the graph with a distinct debug-readback lifetime.
- Register native-window, swap-chain, editor viewport, and backbuffer textures as external graph resources.
- Move physical transient texture allocation behind graph compile output.
- Allocate transient resources from graph lifetime ranges.
- Reuse alias slots for non-overlapping transient resources.
- Prevent aliasing when formats, dimensions, sample counts, or usage flags are incompatible.
- Prevent aliasing across temporal history, readback, native-window, and imported external resources.
- Emit barriers for every required access transition.
- Map graph barriers to bgfx view ordering and resource usage constraints.
- Validate read-before-write and write-after-write hazards.
- Validate write-after-read hazards, read-after-read compatibility, read/write same-pass rules, and external-resource state assumptions.
- Validate pass ordering across shadow, scene, post-process, overlay, and final composite passes.
- Add graph-driven pass culling for disabled or unused passes.
- Add graph-driven optional pass pruning for disabled bloom, TAA, selection, exposure, shadows, overlays, and debug views.
- Add graph debug dump for pass order, resources, lifetimes, aliases, and barriers.
- Add machine-readable render graph dump for CI artifacts and regression diffs.
- Add in-app debug visualization for resource lifetime.
- Add in-app debug visualization for alias slots.
- Add in-app debug visualization for pass timing and target memory.
- Add CPU pass profiling timestamps.
- Add GPU pass timing where supported by bgfx.
- Expose render graph compile stats in renderer runtime stats.
- Add unit tests for alias compatibility rules.
- Add unit tests for barrier generation.
- Add unit tests for pass culling and disabled pass validation.
- Add regression tests for multi-viewport graph compilation.

## Render graph execution

- Add render graph pass execution callbacks that receive typed resources and pass parameters instead of manually reading renderer globals.
- Add graph executor scheduling that applies compiled pass order, barriers, view ids, debug labels, and resource bindings consistently.
- Add graph-side external resource registration for backbuffer, native-window outputs, scene targets, imported textures, and persistent histories.
- Add graph-side transient texture creation from compiled lifetimes and alias slots.
- Add graph-side transient buffer creation from compiled lifetimes and alias slots.
- Add graph-side resource clear operations with explicit color, depth, stencil, and load/store policy.
- Add graph-side load/store metadata for color, depth, stencil, resolve, discard, and preserve operations.
- Add graph-side framebuffer creation and reuse for every render target attachment set.
- Add graph-side pass culling that preserves required debug/touch/clear passes and culls unused optional passes.
- Add graph-side subresource tracking for mip levels, array slices, cube faces, and temporal ping-pong resources.
- Add graph support for compute passes with read/write buffers, read/write images, dispatch dimensions, and capability gating.
- Add graph support for copy/blit/readback passes with explicit source/destination state transitions.
- Add graph support for async readback requests with frame latency and non-stalling result consumption.
- Add graph validation for resource format compatibility across producers and consumers.
- Add graph validation for pass attachment compatibility: color/depth formats, sample count, dimensions, and array layers.
- Add graph validation for output color space transitions: scene-linear HDR, post-HDR, display-linear, display-encoded LDR.
- Add graph validation for temporal resources that must persist across frames and must not alias transients.
- Add graph validation for multi-viewport view id ranges and detached viewport stride overflow.
- Add graph validation for pass names, debug marker names, resource names, and stable ids.
- Add graph replay/debug dump that can reconstruct frame pass order and resource states from CI artifacts.
- Add render graph unit tests for external resources, persistent history resources, transient aliasing, compute passes, copy passes, and readback passes.
- Add render graph fuzz tests for invalid pass/resource declarations and deterministic diagnostics.

## Production-grade renderer architecture

- Define the target renderer layering explicitly: Backend/RHI, RenderCore, Renderer, EditorRenderer, and tools.
- Move bgfx-specific setup, resource creation, shader loading, and native-window interop behind the Backend/RHI layer.
- Keep ECS, prefab assets, editor state, and material assets free from direct bgfx handle dependencies.
- Introduce renderer-owned primitive/light/camera scene proxies with stable ids, dirty flags, version counters, bounds, and cached draw data.
- Add a compact scene proxy store that supports add/update/remove without rebuilding all render state every frame.
- Add cached mesh draw commands keyed by mesh, material, section, pass, shader variant, and render state.
- Invalidate cached draw commands only when relevant mesh, material, transform, visibility, pass, or shader state changes.
- Add render dependency tracking from ECS components and assets to render proxies, resources, material variants, and pass caches.
- Add explicit frame phases: extract/sync, graph build, graph compile, upload, execute, readback consume, present, and deferred destroy.
- Add renderer-owned per-view state for camera matrices, jitter, exposure, temporal history, visibility results, debug mode, and quality tier.
- Add per-scene renderer state for resource residency, shadow cache, reflection/GI probes, material cache, and draw command cache.
- Add per-backend capability negotiation before quality-tier selection, with human-readable diagnostics for every downgraded feature.
- Add RenderDoc/PIX/Metal capture-friendly naming for every resource, view, pass, shader, material variant, and draw category.
- Add a strict dev validation mode that treats silent fallback, missing graph declarations, ignored material features, and missing resources as errors.
- Add a shipping validation mode that records diagnostics without aborting, with deterministic fallback resources and feature decisions.
- Add renderer architecture documentation that maps each production feature to owning module, graph pass, resources, stats, tests, and debug view.

## Scene renderer and visibility architecture

- Add renderer-owned scene proxy ids independent from ECS entity ids, with stable mapping and stale-id validation.
- Add primitive scene proxies for mesh renderers with world bounds, local bounds, material slots, visibility, shadow flags, motion data, and owner scene id.
- Add light scene proxies with type, color, intensity, range, cone angles, area shape, shadow flags, volumetric flags, and versioning.
- Add camera/view proxies with projection, view, jitter, exposure settings, post-process profile, near/far policy, and camera-cut markers.
- Add environment/sky/probe proxies with resource handles, bounds, priority, intensity, and fallback policy.
- Add decal proxies with volume transform, projection mode, material, blend mode, layer mask, priority, and culling bounds.
- Add particle/VFX proxies with bounds, simulation state, material variants, sort keys, and GPU buffer handles.
- Add terrain proxies with clipmap state, sector bounds, material layers, virtual texture state, and LOD ranges.
- Add vegetation proxies with cell bounds, species/material ids, wind parameters, LOD data, and impostor metadata.
- Add skinned mesh proxies with skeleton palette, previous pose, morph weights, material slots, and pass eligibility.
- Add proxy dirty flags for transform, visibility, mesh, material, lighting, shadow, animation, bounds, and render-state changes.
- Add incremental scene sync that only updates changed proxies and removes dead proxies deterministically.
- Add proxy compaction or sparse storage policy that avoids invalidating stable ids unexpectedly.
- Add broad-phase spatial structure for primitive, light, decal, probe, terrain, vegetation, and debug-picking queries.
- Add CPU frustum culling per view using scene proxy bounds and per-pass visibility flags.
- Add occlusion culling state per view with frame-lagged results and conservative fallback.
- Add draw command cache keyed by pass, mesh, section, material, shader variant, render state, and vertex/index layout.
- Add draw command invalidation when any dependent resource, proxy flag, material variant, or shader permutation changes.
- Add pass-specific visibility masks for base pass, depth prepass, shadow, selection, velocity, translucency, decals, and debug views.
- Add per-view visibility results with visible primitive count, culled primitive count, occluded primitive count, and skipped reason counters.
- Add deterministic sorting for opaque, masked, transparent, decals, particles, editor overlays, and debug draws.
- Add scene renderer tests for proxy lifecycle, dirty propagation, visibility masks, culling, sorting, and command cache invalidation.

## GPU-driven render pipeline

- Create GPU-visible instance, bounds, meshlet, LOD, and draw-command buffers. (partial: GPU-visible bounds + packed metadata exist; meshlet-geometry and indirect draw-command buffers still missing)
- Build hierarchical Z buffer inputs from the current depth buffer.
- Dispatch HZB seed and downsample compute passes.
- Dispatch instance occlusion culling against HZB.
- Compact visible instances into a GPU-visible draw list.
- Generate indirect draw arguments on GPU.
- Submit indirect draws from generated draw arguments.
- Dispatch meshlet or cluster culling for GPU-driven meshes.
- Cull meshlets by frustum, cone backface, and occlusion data.
- Select LOD on GPU using screen coverage and per-mesh thresholds.
- Keep CPU LOD selection as validation and fallback.
- Expose GPU culling, indirect draw, meshlet, and LOD counters in submit stats.
- Add GPU culling debug readback for visible instance counts.

## Shader system and material compiler

- Add shader source registry with stable shader ids, stages, entry points, include graph, defines, backend profiles, and debug names.
- Add shader permutation key schema for material features, mesh features, lighting path, shadow mode, skinning, morphing, instancing, terrain, vegetation, decals, and post-process.
- Add shader compile manifest that records every required permutation per backend and quality tier.
- Add offline shader build step that emits binaries, reflection metadata, binding layouts, permutation manifests, and diagnostics.
- Add runtime shader lookup by permutation key with deterministic fallback to safe variants and explicit missing-permutation diagnostics.
- Add shader reflection data for uniforms, samplers, storage buffers, images, vertex attributes, and render target formats.
- Add material compiler that maps asset properties and textures to shader feature bits, binding layouts, default resources, and validation messages.
- Add material variant cache keyed by material asset id, shader feature set, backend, quality tier, and pass type.
- Add material binding layout validation for every shader permutation and every material feature combination.
- Add unsupported material feature policy: hard error, editor warning, runtime fallback, or disabled feature with stats.
- Add shader hot-reload in editor with permutation cache invalidation and safe fallback while recompiling.
- Add shader compile budget metrics for permutation count, binary size, compile time, reflection size, and runtime cache size.
- Add shader tests for permutation key stability, manifest coverage, fallback selection, binding layout compatibility, and missing shader diagnostics.
- Add material compiler tests for every advanced PBR feature, texture default, unsupported feature combination, and backend fallback.

## Mesh, geometry, and asset pipeline

- Add mesh asset version metadata with vertex streams, index streams, sections, material slots, bounds, LODs, meshlets, and collision handoff.
- Add mesh import validation for positions, normals, tangents, UVs, colors, joints, weights, morph targets, and index ranges.
- Generate missing tangents with a deterministic MikkTSpace-compatible path or explicit unsupported diagnostics.
- Generate mesh bounds per section, per LOD, per meshlet, and per asset with validation against invalid or NaN data.
- Add mesh optimization pipeline: vertex cache optimization, overdraw optimization, vertex fetch optimization, and index remapping.
- Add meshlet generation pipeline with cone data, bounds, LOD association, and backend limits.
- Add LOD group asset data with screen-size thresholds, hysteresis, forced LOD, and editor preview override.
- Add CPU LOD selection tests for distance, screen coverage, hysteresis, forced LOD, and missing LOD fallback.
- Add GPU LOD input buffer generation and validation against CPU LOD selection.
- Add static mesh streaming chunks for vertex/index buffers, meshlets, materials, and fallback low LODs.
- Add placeholder mesh policy for missing, streaming, invalid, or unsupported geometry.
- Add mesh resource residency tracking, pinned state, last-used frame, memory size, and eviction policy.
- Add async mesh load and GPU upload path with completion fences and render-scene synchronization.
- Add mesh hot-reload that updates resource handles, draw command caches, bounds, and material slots safely.
- Add mesh asset tests for malformed files, invalid sections, invalid material slots, missing tangents, huge meshes, and streaming chunks.
- Add mesh golden scenes for UV seams, tangent-space normals, alpha masking, LOD transitions, instancing, and shadow casting.

## Texture and image pipeline

- Add texture asset metadata for dimensions, format, mip count, array layers, cube faces, color space, compression, and intended usage.
- Add texture import validation for dimensions, block alignment, mip completeness, color space, HDR ranges, and normal-map encoding.
- Add backend texture format selection for BCn, ETC2, ASTC, RGBA8, RGBA16F, R11G11B10F, R16F, R32F, depth, and stencil formats.
- Add texture fallback policy per usage: white, black, flat normal, roughness, metallic, AO, emissive, LUT, shadow, probe, and missing debug.
- Add texture streaming with mip residency, upload queue, eviction, pinned mips, visible-priority requests, and budget diagnostics.
- Add virtual texture groundwork for terrain/material layers with page table, feedback, residency, and fallback pages.
- Add cubemap import and validation for skyboxes, irradiance, prefiltered radiance, reflection probes, and point-light shadows.
- Add 3D texture and LUT import validation for color grading, volumetrics, noise, and simulation data.
- Add sRGB/linear sampling validation for albedo, emissive, normal, roughness, metallic, AO, masks, LUTs, and HDR textures.
- Add texture hot-reload with dependency invalidation for materials, probes, LUTs, skyboxes, and post-process profiles.
- Add async texture upload tests, streaming residency tests, compression tests, fallback tests, and color-space tests.

## Render passes and frame pipeline completeness

- Add depth prepass with masked/opaque policy, depth format validation, and fallback when depth prepass is disabled.
- Add normal/roughness/material-id prepass or G-buffer path for deferred, SSAO/GTAO, SSR, and debug views.
- Add base opaque pass with physically based lighting, shadows, IBL, decals, lightmaps, AO, and material variants.
- Add masked pass with alpha cutoff, dithering, shadow/depth variants, and deterministic sorting.
- Add transparent pass with back-to-front sorting, weighted blended OIT option, refraction hooks, and depth-aware composition.
- Add velocity pass for camera, rigid object, skinned mesh, morph target, vegetation wind, and particles.
- Add selection/id pass for editor picking, outline mask, and hit proxy diagnostics.
- Add editor overlay pass for grid, gizmos, bounds, icons, debug text, and selection highlights.
- Add final composite pass with output transform, gamma, tonemap, UI-safe composition, and display format validation.
- Add debug-view pass routing that can replace or overlay final output per viewport.
- Add pass enable/disable dependency validation so disabled passes cannot leave dangling graph reads.
- Add pass-level stats for draw calls, dispatches, triangles, instances, resources read/written, timing, and memory footprint.
- Add pass-level error diagnostics with pass name, graph node id, resource id, backend view id, and failure reason.

## GPU HDR luminance histogram and temporal auto-exposure

- Make GPU HDR luminance histogram the default auto-exposure path when compute, HDR target sampling, and readback/storage support are available.
- Keep the current scene-lighting estimator only as a low-end fallback, with explicit stats and diagnostics when it is used.
- Replace the current fragment/readback exposure pass with a compute histogram path that reads the HDR scene color target directly.
- Add histogram clear, luminance accumulation, and reduction passes with render graph resources, barriers, view ids, and debug markers.
- Store histogram bins in a GPU buffer or texture format that preserves enough precision for bright HDR scenes.
- Add percentile clipping controls for low/high luminance outliers before computing metered average luminance.
- Add exposure buffer output with metered luminance, adapted luminance, exposure stops, min/max EV, and debug counters.
- Move temporal adaptation to the GPU when supported, using previous exposure history and delta time.
- Keep CPU temporal adaptation as fallback for devices without compute/readback support.
- Reset exposure history on camera cuts, viewport resize, HDR target recreation, quality-tier changes, and explicit post-process profile changes.
- Feed adapted luminance into tonemap/final composite without waiting on same-frame CPU readback.
- Add one-frame-latency path for debug readback so profiling and UI stats do not stall rendering.
- Add exposure debug view showing HDR luminance, histogram bins, clipped percentile range, metered EV, adapted EV, and final exposure stops.
- Add tests for histogram binning, percentile clipping, adaptation rates, reset rules, fallback selection, and render graph resource declarations.
- Add render smoke scene that switches between dark interior, bright exterior, and high-contrast highlights to validate temporal exposure behavior.

## Post-Process / HDR

- Add HDR pipeline validation that scene color, bloom, post-process final, and display output formats are compatible.
- Add validation that scene HDR targets, post-process intermediates, temporal history, and final composite targets use compatible color spaces.
- Add explicit low-end non-HDR fallback path.
- Add automatic fallback from HDR post-process to LDR-compatible output when HDR targets are unsupported.
- Add exposure histogram compute pass instead of readback-only metering.
- Keep fragment/readback exposure path clearly marked as transitional, with diagnostics that it is not the production GPU histogram path.
- Add GPU luminance downsample chain from HDR scene color for exposure, bloom thresholds, and debug visualization.
- Add compute shader histogram accumulation from downsampled luminance, with configurable bin range and percentile clipping.
- Add auto-exposure source selection between scene-light estimate, HDR readback histogram, and full GPU histogram path.
- Add async GPU exposure readback scheduling without frame stalls.
- Store exposure history per viewport, per scene/camera identity, and per post-process profile instead of as one global renderer state.
- Add eye adaptation settings for bright and dark adaptation per scene.
- Add temporal exposure adaptation with min/max EV, separate up/down speeds, and metering mode controls.
- Reset temporal exposure history on camera cuts, render target recreation, scene changes, and exposure mode switches.
- Reset temporal exposure history on viewport detach, close, reopen, minimize/restore, render-size change, and backend reset.
- Reset or invalidate TAA history on camera cuts, projection changes, jitter mode changes, render target recreation, scene changes, and post-process quality changes.
- Track temporal history with per-viewport generation ids instead of only global frame parity.
- Add camera-cut and discontinuity detection inputs to render submission.
- Add motion-vector validity metadata so TAA can reject history when velocity/depth inputs are missing or stale.
- Add exposure debug view for histogram, metered luminance, adapted luminance, and exposure stops.
- Add tonemap controls for shoulder, toe, saturation, contrast, white point, and per-scene override profiles.
- Add tonemapping calibration scenes and golden screenshots for ACES, neutral, and custom curves.
- Add bloom threshold, knee, radius, and mip-chain debug visualization.
- Add bloom quality controls for mip count, radius, intensity, threshold, soft knee, and firefly suppression.
- Add dirt, lens, or anamorphic bloom option only if art direction requires it.
- Add bloom dirt lens texture asset loading, default fallback texture, sampler state, and material/debug binding.
- Add anamorphic bloom directional blur pass with orientation, stretch, tint, and intensity controls.
- Add color grading LUT asset loading.
- Bind color grading LUT in final composite.
- Add color grading intensity and fallback when LUT is missing.
- Add real LUT asset pipeline validation for 2D strip and 3D LUT layouts, bit depth, color space, and neutral LUT generation.
- Add film grain pass.
- Add vignette pass.
- Add chromatic aberration pass.
- Add optional styling toggles for chromatic aberration, vignette, and film grain per post-process profile and per camera.
- Add sharpen or CAS pass.
- Add SSAO or GTAO pass before lighting or composite.
- Add HBAO quality option or document why GTAO replaces it, with comparable radius/intensity controls.
- Add SSR pass or explicit reflection fallback decision.
- Add SSPR path for planar/water/floor reflections where SSR is insufficient.
- Add hybrid reflection composition that blends SSR/SSPR with reflection probes, sky IBL, and material roughness.
- Add depth of field pass.
- Add bokeh-aware DOF gather/scatter, focus distance, aperture, near/far blur, and circle-of-confusion debug overlay.
- Add DOF focus picking or autofocus support.
- Add motion blur pass.
- Add camera motion blur from previous/current view-projection matrices with shutter angle and sample count controls.
- Add object motion blur from per-object previous transforms and skinned/animated motion-vector support.
- Add camera and object motion vectors for post-process reprojection.
- Persist previous object transforms in render proxies for object motion vectors, TAA rejection, and motion blur.
- Add explicit unsupported diagnostics when only camera/depth motion vectors are available.
- Add jittered projection support in camera matrices and expose per-frame jitter to render views, culling, and reprojection.
- Add TAA resolve using jittered projection, history color, depth rejection, motion vectors, and neighborhood clamping.
- Add TAA history validation and reset rules.
- Add TAA reactive mask or disocclusion rejection.
- Add TSR or temporal upscaling pass with internal resolution scale, sharpening, reactive mask input, and quality presets.
- Add upscaling fallback path for non-TSR devices and verify UI/composite scaling.
- Add post-process quality presets.
- Add render graph resource declarations for every post-process intermediate.
- Add post-process debug views for HDR scene color, luminance, exposure, bloom mips, motion vectors, depth, normals, AO, SSR/SSPR, LUT, and final LDR output.
- Add post-process pass profiling in runtime stats.
- Add render smoke coverage for HDR exposure, bloom pyramid, tonemap, TAA, and LDR fallback.
- Add artifact/golden-image coverage for motion vectors, motion blur, DOF, AO, SSR/SSPR, LUT grading, and stylized post-process toggles.

## Lighting

- Keep directional, point, and spot forward lighting covered by runtime tests.
- Add physically plausible attenuation controls for point and spot lights.
- Add tiled forward lighting path for many local lights.
- Add clustered forward+ lighting path for many local lights.
- Add deferred lighting path option.
- Add visibility-buffer lighting path option.
- Add renderer capability selection between forward, clustered forward+, deferred, and visibility-buffer paths.
- Build per-tile or per-cluster light lists on GPU.
- Add GPU buffers for light records, light grids, and light indices.
- Add CPU fallback light list construction for low-end or compute-disabled devices.
- Support hundreds of visible local lights without per-object uniform limits.
- Support thousands of culled scene lights with bounded GPU memory.
- Add debug counters for total lights, visible lights, clustered lights, and lights skipped by capacity.
- Add debug visualization for tiled or clustered light occupancy.
- Add stress test scene with hundreds of point and spot lights.
- Add stress test scene with thousands of culled lights.
- Add area rectangle light shading.
- Add area disk light shading.
- Add tube light shading.
- Add physically based LTC lookup tables for area lights.
- Bind LTC lookup textures for area light evaluation.
- Add area light diffuse and specular integration in the lit shader.
- Add area light shadowing strategy or explicit unsupported-feature diagnostics.
- Add per-light shadow enable and disable coverage for all shadow-capable lights.
- Add shadow atlas allocation for multiple shadow-casting lights.
- Add point light shadow map support.
- Add spot light shadow map support.
- Add cascaded directional shadows.
- Add stable cascade snapping tests.
- Add PCSS, EVSM, or MSM implementation beyond stats/config values.
- Add contact shadow rendering.
- Add volumetric light scattering rendering.
- Add volumetric froxel grid or raymarch pass.
- Add volumetric temporal reprojection and history rejection.
- Add volumetric quality settings and low-end fallback.
- Add clustered forward light assignment on GPU.
- Add clustered light list buffers and debug stats.
- Add fallback from clustered forward to simple forward lighting.
- Add exposure-meter tests for advanced light types.
- Add render smoke scenes for directional shadows, local lights, area lights, contact shadows, and volumetrics.

## Shadow system

- Split shadow setup into a reusable shadow system instead of a single directional planner.
- Allocate a shadow atlas for directional, spot, and point shadow maps.
- Pack multiple shadow-casting lights into the atlas.
- Track atlas tile allocation, eviction, and reuse.
- Add per-light shadow cache keys.
- Reuse unchanged shadow maps across frames when per-light caching is enabled.
- Invalidate cached shadow maps when caster transforms, caster meshes, light transforms, or shadow settings change.
- Add cascaded shadow maps for directional lights.
- Compute cascade split distances from camera near/far and shadow distance.
- Stabilize cascade projection with texel snapping.
- Blend or fade between cascades.
- Add per-cascade culling of shadow casters.
- Add cascade debug visualization.
- Implement EVSM shadow filtering.
- Implement MSM shadow filtering.
- Implement PCSS blocker search and penumbra filtering.
- Add shadow filter shader variants and capability fallbacks.
- Add shadow bias controls per light type.
- Add normal bias and slope-scaled bias controls.
- Add receiver-side shadow fade by distance.
- Add runtime stats for atlas usage, cached shadow hits, cached shadow misses, cascade count, and filter cost.
- Add unit tests for shadow atlas allocation.
- Add unit tests for shadow cache invalidation.
- Add unit tests for cascade split and stable snapping.
- Add render smoke coverage for CSM, atlas packing, EVSM/MSM/PCSS, and cache reuse.

## Production PBR materials

- Add a renderer-visible unsupported-material-feature bitset for clearcoat, sheen, transmission, subsurface, anisotropy, decals, layers, and future extensions. (partial: per-feature ParsedButIgnored classification + warnings exist; single packed bitset type still missing)
- Pack clearcoat material factors into shader uniforms or material buffers.
- Bind clearcoat and clearcoat roughness textures in the mesh submit path.
- Implement clearcoat BRDF lobe in the lit mesh shader.
- Pack sheen color and roughness into shader inputs.
- Bind sheen color textures.
- Implement sheen BRDF contribution.
- Pack transmission, thickness, attenuation color, and attenuation distance.
- Bind transmission and thickness textures.
- Implement transmission lighting path for transparent materials.
- Pack subsurface color and subsurface factor.
- Implement a subsurface approximation for forward lighting.
- Pack anisotropy strength and rotation.
- Bind anisotropy textures.
- Implement anisotropic specular in the PBR shader.
- Bind decal textures and decal blend mode.
- Implement decal projection or mesh-surface decal blending.
- Bind layer mask textures and layer blend mode.
- Implement layered material blending.
- Add material variant keys for advanced PBR feature combinations.
- Add validation for unsupported material feature combinations.
- Add material asset versioning and migration for newly introduced PBR feature fields.
- Add inspector/debug output that shows which material features are active, unsupported, or falling back.
- Add shader tests or render smoke scenes for clearcoat, sheen, transmission, subsurface, anisotropy, decals, and layered materials.
- Add asset-loader tests for every advanced texture path and factor.

## IBL pipeline

- Add environment asset type for HDR/cubemap source data.
- Generate irradiance cubemaps for diffuse IBL.
- Generate prefiltered radiance cubemaps for GGX specular IBL.
- Generate or load BRDF integration LUT.
- Add runtime resource ownership for IBL textures.
- Bind irradiance map, prefiltered environment map, and BRDF LUT in scene submit.
- Sample irradiance map for diffuse environment lighting.
- Sample prefiltered environment map by roughness for specular reflections.
- Use BRDF LUT for split-sum specular IBL.
- Add reflection probe resource registration.
- Add reflection probe extraction from scene data.
- Select the most relevant reflection probes per view or per object.
- Blend local reflection probes.
- Implement box-probe parallax correction.
- Implement sphere-probe parallax correction.
- Add fallback to hemisphere or constant environment when IBL resources are missing.
- Expose IBL, probe, and parallax stats in runtime stats.
- Add tests for probe selection and fallback behavior.
- Add render smoke coverage for IBL and local probes.

## Global illumination

- Add GI mode selection to renderer runtime settings.
- Implement SSGI screen-space tracing pass.
- Add SSGI denoise pass.
- Add SSGI temporal accumulation.
- Add SSGI fallback to ambient or IBL on low-end devices.
- Add DDGI probe volume component or scene data.
- Add DDGI probe irradiance and visibility textures.
- Add DDGI probe update pass.
- Add DDGI probe relocation or validity handling.
- Add DDGI sampling in the lit shader.
- Add static probe grid GI mode.
- Add probe grid baking or import path.
- Add lightmap asset type.
- Add lightmap UV and texture binding path for static meshes.
- Add lightmap sampling in material shaders.
- Add mixed lighting path for baked GI plus dynamic direct lighting.
- Add GI quality presets.
- Add low-end fallback from DDGI or SSGI to probe grid, lightmaps, IBL, or constant ambient.
- Add GI debug visualization for probes, irradiance, visibility, and screen-space traces.
- Add runtime stats for GI mode, probe count, updated probes, SSGI ray count, and fallback reason.
- Add tests for GI mode selection and fallback.
- Add render smoke coverage for SSGI, DDGI or probe grid, and lightmaps.

## Translucency, particles, and VFX

- Add transparent material classification for alpha blend, additive, multiplicative, premultiplied, refractive, and distortion modes.
- Add transparent sorting keys by layer, material priority, distance, depth bucket, and stable tie-breaker.
- Add weighted blended OIT path with accumulation and revealage targets.
- Add fallback sorted alpha blending path for devices without required OIT support.
- Add refraction pass that samples scene color/depth with roughness and thickness support.
- Add soft particles with depth fade, camera fade, near-plane fade, and motion vectors where possible.
- Add GPU particle buffer layout for positions, velocities, colors, sizes, rotations, lifetimes, and sort keys.
- Add CPU fallback particle simulation and submit path for low-end devices.
- Add GPU particle simulation dispatch with dead/alive lists, emit counters, and indirect draw support.
- Add particle sorting for transparent particles with CPU fallback and GPU sort path.
- Add ribbon/trail rendering path with camera-facing and velocity-facing modes.
- Add beam rendering path with noise, tapering, UV scrolling, and depth fade.
- Add sprite sheet animation, flipbook blending, and texture atlas validation.
- Add VFX budget stats for emitters, particles, draw calls, simulation time, GPU memory, and dropped particles.
- Add render smoke scenes for alpha blend, additive, refractive, soft particles, ribbons, beams, and GPU/CPU fallback.

## Decals and surface projection

- Add decal component and render proxy with transform, bounds, material, blend mode, projection depth, normal threshold, and sort order.
- Add mesh decals or projected decals path depending on available G-buffer/depth data.
- Add deferred decal path for base color, normal, roughness, metallic, AO, emissive, and mask channels.
- Add forward decal fallback for platforms or paths without deferred/G-buffer targets.
- Add decal atlas/material binding validation and fallback textures.
- Add decal culling against view frustum, receiver bounds, and decal volume.
- Add decal layering and priority conflict resolution with deterministic order.
- Add decal debug view for bounds, affected receivers, atlas page, blend mode, and overdraw.
- Add decal tests for projection math, culling, sorting, missing material fallback, and feature gating.

## Cameras, views, and render settings

- Add render view descriptor that owns camera matrices, viewport rect, scissor, jitter, exposure, post-process profile, quality tier, and debug view.
- Add camera component fields for exposure mode, post-process profile, FOV, near/far, projection mode, jitter enable, and camera cut id.
- Add orthographic camera support in scene, shadows, culling, post-process reprojection, and editor views.
- Add reverse-Z and depth range policy validation per backend.
- Add view-family concept for multiple views sharing scene data, shadows, probes, and render graph resources.
- Add stereo/VR-ready view descriptors even if implementation remains disabled.
- Add split-screen/multi-camera render submission tests.
- Add per-camera render settings overrides for shadows, post-process, resolution scale, debug view, and quality tier.
- Add camera cut detection and explicit camera cut API for gameplay/editor callers.
- Add render settings assets/profiles with versioning, validation, fallback, and editor inspection.
- Add tests for perspective, orthographic, resize, camera cuts, jitter sequences, and per-camera overrides.

## Skinned Animation Renderer

- Replace current skinned mesh rejection path with runtime support for skinned vertex formats.
- Add skeleton, joint, inverse-bind, and animation clip asset loading from glTF.
- Add CPU animation sampling for joints, clips, blending, masks, and root motion extraction.
- Add GPU skinning path using bone matrix or dual-quaternion buffers.
- Add skinned mesh submit path with per-instance skeleton palette binding.
- Add skinned shadow, depth, velocity, selection, and motion-vector passes.
- Add morph target asset loading, weights, GPU buffers, and shader deformation path.
- Add combined skinning plus morph target path with deterministic ordering.
- Add previous-frame skinned transform storage for TAA and object motion blur.
- Add cloth simulation/rendering hooks for external simulation buffers or future solver integration.
- Add hair rendering hooks for strand/card assets, simulation input, shadowing, and material variants.
- Add animation renderer stats for skinned meshes, joints, palettes, morph targets, upload bytes, and GPU skinning cost.
- Add tests for glTF skin import, rejection fallback removal, skeleton palette validation, morph targets, and skinned pass routing.

## Geometry / World

- Add world partitioning or chunk/sector system for large scenes, with streaming-friendly bounds and ownership.
- Add static mesh streaming by world sector, including load/unload transitions and render-scene synchronization.
- Add world origin rebasing or floating-origin support for large coordinate spaces.
- Add spatial acceleration structure for world geometry queries, visibility, probe lookup, decals, and debug selection.
- Add CPU/GPU occlusion data for world sectors and large static occluders.
- Add terrain support with heightmap import, material layer masks, LOD, collision handoff, and streaming.
- Add terrain renderer with geometry clipmaps for camera-relative large terrain.
- Add terrain mesh LOD fallback for platforms without tessellation or compute-driven terrain.
- Add terrain tessellation or displacement path where backend capabilities support it.
- Add terrain virtual texturing or page-table based material streaming for large splat/layer textures.
- Add terrain normal, height, hole, and material-layer asset import validation.
- Add terrain debug views for clipmap rings, selected LOD, virtual texture residency, and displacement range.
- Add instanced foliage/vegetation rendering with GPU culling, LODs, wind parameters, and impostor fallback.
- Add vegetation instance asset format with per-instance transform, bounds, random seed, species/material id, and LOD data.
- Add hierarchical vegetation culling by cell/cluster before per-instance culling.
- Add vegetation wind animation parameters and shader deformation path.
- Add vegetation impostor asset generation or import path with billboard/hemisphere atlas support.
- Add vegetation LOD transition strategy with dithering or cross-fade to avoid popping.
- Add vegetation shadow and depth-only variants for instanced, wind-deformed, and impostor geometry.
- Add road/path/spline geometry generation or import path if the world pipeline requires authored splines.
- Add world decal volumes with streaming, culling, atlas/material binding, and editor/debug visualization.
- Add reflection, GI, and light probe volumes as world entities with sector streaming and lookup acceleration.
- Add water surface geometry path with reflection/refraction hooks, depth interaction, and LOD.
- Add sky/atmosphere world component integration with render settings, exposure, and time-of-day data.
- Add per-sector lighting metadata for static lights, probes, shadow cache invalidation, and fallback budgets.
- Add async world chunk loading and unloading pipeline decoupled from render submission.
- Add async GPU asset upload queue for streamed meshes, textures, terrain pages, vegetation cells, and probes.
- Add residency budgets for streamed world chunks, meshes, textures, virtual texture pages, probes, and foliage instances.
- Add eviction policy for over-budget streamed assets with pinned and recently-visible resource handling.
- Add streaming priority model based on camera position, predicted movement, visibility, and gameplay/editor pins.
- Add visible placeholder or fallback resources for chunks and assets still uploading.
- Add editor/debug visualization for world sectors, bounds, LOD ranges, occluders, probes, decals, and streaming state.
- Add world asset budget reporting for geometry memory, textures, instances, probes, and streamed sectors.
- Add tests for world partition loading/unloading, origin rebasing math, sector bounds, and render-scene sync.
- Add tests for terrain clipmap LOD selection, virtual texture residency, and terrain fallback path.
- Add tests for vegetation cell culling, wind shader variant selection, impostor fallback, and LOD transitions.
- Add tests for streaming priority, budget eviction, async upload completion, and placeholder replacement.

## Render debugging and visualization

- Add in-app render debug menu with per-viewport view selection and capture/export controls.
- Add debug view framework with stable ids, names, required resources, and fallback if resource is unavailable.
- Add debug view for base color, metallic, roughness, normal, tangent, world position, depth, linear depth, stencil, material id, object id, motion vectors, velocity magnitude, exposure, luminance, bloom mips, shadow maps, shadow cascades, light clusters, probes, decals, AO, SSR, GI, overdraw, wireframe, bounds, LOD, meshlets, and streaming residency.
- Add debug overlays for draw call count, pass timings, render target memory, upload bytes, resource budgets, and feature fallbacks.
- Add selectable object debug panel showing render proxy id, mesh, materials, shader variant, bounds, LOD, visibility, lights, shadows, and resource residency.
- Add render graph debug viewer with pass order, resources, aliases, barriers, lifetimes, and pass timings.
- Add shader/material debug viewer with active permutation, defines, textures, uniforms, and unsupported feature diagnostics.
- Add live capability report panel with backend, limits, supported features, quality tier, disabled features, and fallback reasons.
- Add frame capture button that exports graph dump, stats, screenshots, capability report, and relevant logs.
- Add debug visualization tests or smoke screenshots for every debug view category.

## Quality tiers, scalability, and runtime settings

- Define low, medium, high, ultra, cinematic, editor, and custom quality tiers.
- Map every renderer feature to quality tier defaults, backend requirements, memory budget, and fallback policy.
- Add feature override system that explains why requested features are enabled, disabled, or downgraded.
- Add dynamic resolution policy with min/max scale, target frame time, hysteresis, camera-cut/history invalidation, and upscaler integration.
- Add dynamic quality scaler hooks for shadows, GI, AO, SSR, bloom, translucency, particles, vegetation, terrain, texture streaming, and resolution.
- Add quality preset serialization and editor controls with validation and backend-specific diagnostics.
- Add runtime stats for active tier, active overrides, disabled features, fallback reasons, and dynamic scaler decisions.
- Add tests for tier selection, backend fallback, forced overrides, dynamic scaling, history invalidation, and deterministic diagnostics.

## Memory, residency, and streaming

- Add global render memory budget with separate pools for render targets, buffers, textures, shaders, materials, streamed assets, and staging.
- Add per-scene and per-world render memory budgets with editor overrides.
- Add resource residency tracking with last-used frame, visible priority, pinned state, streaming state, and owner scene.
- Add eviction policy for textures, meshes, probes, terrain pages, vegetation cells, material variants, and shader permutations.
- Add over-budget diagnostics with responsible resources, eviction candidates, pinned resources, and fallback actions.
- Add async IO/decode/upload queues for textures, meshes, materials, shaders, probes, lightmaps, terrain, vegetation, and animation data.
- Add upload throttling and stall detection for frame-time budget compliance.
- Add placeholder replacement path when streamed resources finish uploading.
- Add resource dependency graph so evicting a texture/material/mesh invalidates dependent draw commands safely.
- Add tests for budget enforcement, eviction order, pinned resources, async upload completion, placeholder replacement, and scene release cleanup.

## Performance / Platform

- Split renderer layering into bgfx backend/RHI wrappers, render core, and scene renderer modules.
- Add a backend-facing RHI/resource abstraction so higher-level renderer code does not create, destroy, or name raw bgfx handles directly.
- Add typed RAII wrappers for bgfx textures, buffers, framebuffers, shaders, programs, uniforms, and swap-chain/native-window handles.
- Add a central deferred GPU destroy queue for every bgfx resource kind, with frame fences and immediate shutdown mode.
- Add render target and render buffer pools for scene, shadow, post-process, temporal, readback, and editor viewport resources.
- Add a render command queue boundary between game/editor code and render submission.
- Add optional render thread execution model with deterministic single-thread fallback.
- Add frame fences and resource lifetime validation across game, render, and bgfx frame completion.
- Add upload command batching for static assets, dynamic instance data, material buffers, light lists, and readback requests.
- Add GPU crash/debug breadcrumb labels for frame, viewport, graph pass, shader program, material variant, and render target.
- Add renderer validation layer toggles for dev, test, CI, and shipping configurations.
- Add frame profiler timeline for CPU pass time, render-thread work, GPU pass time, waits, and present cost.
- Add CPU timing scopes for every render graph pass, scene pass, shadow pass, post-process pass, upload phase, and present phase.
- Add GPU timing scopes per render graph pass and expose unresolved/unsupported timestamp states cleanly.
- Correlate CPU and GPU pass timings by frame id, view id, pass name, and render graph node.
- Add bgfx GPU timestamp integration where supported and explicit unavailable-state reporting where not supported.
- Add debug marker scopes around every bgfx view/pass, compute dispatch, draw batch category, and resource upload phase.
- Add RenderDoc-friendly capture labels for frame, viewport, render graph pass, shader program, material variant, and target resource.
- Add runtime capture trigger integration for RenderDoc or backend capture APIs where available.
- Add per-pass and per-resource memory stats for render targets, transient graph resources, buffers, textures, and upload staging.
- Add global render memory budget configuration for transient render targets, static buffers, dynamic buffers, textures, streamed assets, and staging uploads.
- Add transient buffer allocator for per-frame constants, instance data, draw args, light lists, skinning palettes, and compute scratch data.
- Add transient buffer lifetime validation and aliasing diagnostics similar to transient texture graph resources.
- Add texture compression import pipeline for BCn, ETC2, ASTC, and uncompressed fallback targets.
- Add texture compression format selection per backend, platform tier, alpha mode, normal maps, HDR textures, and quality preset.
- Add validation for compressed texture mip chains, block alignment, color space, and runtime format support.
- Add runtime performance HUD or debug panel for frame time, draw calls, triangles, instances, lights, shadows, post-process, streaming, and memory budgets.
- Add automated performance budgets for frame time, CPU render time, GPU time, draw calls, triangles, upload bytes, memory, shader compile time, and streaming stalls.
- Fail or warn in CI when performance budgets regress beyond configured tolerance per scene, backend, and quality tier.
- Add automated performance smoke scenes for many meshes, many lights, heavy post-process, terrain, vegetation, skinning, and streaming.
- Add benchmark harness output in machine-readable JSON for CI/regression tracking.
- Add platform capability matrix for D3D11, D3D12, Vulkan, Metal, and headless/noop backends.
- Add backend quality tiers that map D3D11, D3D12, Vulkan, Metal, and Noop capabilities to concrete renderer feature presets.
- Add quality-tier override and diagnostics so forced low/medium/high/ultra modes explain every disabled feature.
- Add feature gating for compute, indirect draws, texture arrays, texture formats, HDR targets, timestamps, tessellation, and readback support.
- Add backend-specific fallback validation for features that are not portable across all bgfx renderers.
- Add shader permutation budget reporting and missing-permutation diagnostics per backend profile.
- Add material compiler diagnostics for unsupported feature combinations, missing textures, missing shader variants, and backend capability fallbacks.
- Add offline shader/material build step that emits permutation manifests for every supported backend profile.
- Add runtime shader permutation cache with deterministic fallback to safe variants.
- Add async upload/staging buffer budgeting and stall detection.
- Add resource lifetime telemetry for cached assets, streamed assets, evictions, and residency misses.
- Add CPU job/threading model for asset IO, decompression, culling preparation, animation sampling, and streaming decisions.
- Add synchronization diagnostics for render submission stalls, readbacks, asset uploads, and frame pacing.
- Add dynamic resolution scaling controller with target frame time, min/max scale, hysteresis, history invalidation, and upscaler integration.
- Add dynamic quality scaler hooks for resolution, shadows, post-process, GI, vegetation density, terrain LOD, and streaming budgets.
- Add visual debug view framework with selectable fullscreen views and per-viewport routing.
- Add visual debug views for world normals, view normals, roughness, metallic, base color, material id, depth, linear depth, motion vectors, light clusters, exposure, bloom mips, overdraw, shadows, AO, SSR, and probe selection.
- Add overdraw visualization pass for opaque, transparent, vegetation, particles, decals, and editor overlays.
- Add low-memory and low-end hardware presets with deterministic feature fallback decisions.
- Add platform tests for renderer initialization, resize/minimize/restore, device loss or reset handling where available, and backend selection.
- Add CI artifacts for render smoke screenshots, profiler captures, capability reports, and performance summaries.

## Multi-viewport and editor presentation quality

- Make every editor viewport own independent scene target, post-process targets, temporal history, exposure history, selection mask, and final composite target.
- Add per-viewport lifecycle tests for dock, undock, redock, close, reopen, resize, minimize, restore, DPI change, and parent-window change.
- Add multi-viewport render smoke that renders docked and detached viewports with different scenes/cameras/post-process settings in one frame.
- Add validation that view ids, temporal histories, exposure histories, readbacks, and debug views never cross-contaminate between viewports.
- Add editor presentation backend that can present bgfx output without GDI blit quality loss where the platform supports it.
- Add native-window/swap-chain fallback diagnostics when multiple window presentation is unsupported.
- Add DPI-aware viewport sizing, safe-area preview, and pixel-perfect/fill/fit present validation in GPU smoke.
- Add screenshot capture per viewport, including detached windows, with metadata for viewport id, backend, resolution, DPI, and quality tier.
- Add editor viewport capture artifacts to CI for failing GPU smoke and golden tests.

## Engine, asset, and editor render contract

- Add schema/version metadata for render-facing components, material assets, mesh assets, light components, prefab nodes, and renderer settings.
- Add migration tests for older prefab/material/light assets when new render fields are introduced.
- Add round-trip prefab tests for every render-facing component field, including default values and omitted optional fields.
- Add prefab override/apply/revert tests for every render-facing light, mesh, material, shadow, and post-process property.
- Add editor inspector controls or explicit read-only diagnostics for every render-facing component property.
- Add editor warnings when a user selects a feature unsupported by the current backend or quality tier.
- Add asset import diagnostics that distinguish unsupported, missing, malformed, and intentionally ignored render features.
- Add material and mesh asset dependency tracking so texture/material/mesh changes invalidate only affected render resources and draw commands.
- Add hot-reload tests for materials, textures, meshes, shaders, and prefabs while a scene viewport is active.
- Add deterministic fallback asset policy for missing meshes, textures, materials, shaders, LUTs, probes, and streamed resources.
- Add a visible editor/debug placeholder path for missing resources that is disabled or configurable for shipping.
- Add compatibility tests that old scenes/prefabs continue to load with new render component fields and default feature fallbacks.

## Testing / Tooling

- Make render smoke coverage mandatory in at least one CI/nightly lane instead of only an optional local script flag.
- Add a CI matrix that separates fast unit tests, GPU smoke, golden images, performance budgets, and shader/material build validation.
- Add test tags so GPU, slow, golden, performance, and platform-specific render tests can be selected deterministically.
- Add renderer failure triage artifacts: graph dump, capability report, shader manifest report, runtime stats, screenshot, and relevant logs.
- Add render graph test fixtures that validate pass ordering, barriers, aliases, resource lifetimes, and debug marker names together.
- Add profiler tests that verify every enabled render pass emits CPU timing, GPU timing state, memory stats, and stable pass identifiers.
- Add RenderDoc/debug-marker smoke test or validation hook that checks marker coverage for scene, shadow, post-process, compute, and upload passes.
- Add dynamic resolution tests for scale changes, history invalidation, viewport resize, UI composition, and low-end fallback.
- Add backend capability matrix tests for D3D11, D3D12, Vulkan, Metal, and Noop using mocked bgfx caps.
- Add quality tier snapshot tests that lock feature decisions for low, medium, high, ultra, and custom tiers.
- Add memory budget tests for transient textures, transient buffers, upload staging, streamed assets, eviction, and over-budget diagnostics.
- Add texture compression pipeline tests with BCn, ETC2, ASTC, invalid block alignment, missing mips, normal maps, and HDR assets.
- Add shader permutation tests for key generation, manifest coverage, missing variants, backend fallback, and permutation budget limits.
- Add material compiler tests for feature flags, binding layouts, texture defaults, unsupported combinations, and deterministic fallback variants.
- Add golden render scenes for PBR features, IBL/probes, shadows, GI, post-process, terrain, vegetation, skinning, and streaming fallback.
- Add golden screenshot comparison tests per backend and quality tier with tolerance profiles for D3D11, D3D12, Vulkan, Metal, and Noop/headless substitutes.
- Store golden baselines with metadata for backend, driver, quality tier, resolution, exposure mode, seed, and asset manifest hash.
- Add golden screenshot update workflow with reviewable diff artifacts, heatmaps, and failure thumbnails.
- Add GPU smoke harness presets for each supported backend that can run headless/noop where real GPU capture is unavailable.
- Add GPU smoke tests for multi-window editor viewports using docked and detached native-window framebuffers in the same frame.
- Add GPU smoke coverage for detached viewport resize, minimize/restore, close/reopen, swap-chain fallback, and per-viewport temporal history isolation.
- Add automated performance budget tests that consume profiler JSON and compare against checked-in or CI-provided thresholds.
- Add shader compile CI for every shader profile, including dxbc, dxil, spirv, spirv16, essl, glsl, and metal outputs.
- Add shader compile CI diagnostics for compiler version, profile target, defines, include graph, generated binary size, and missing permutation keys.
- Add visual debug view tests or smoke captures for normals, roughness, metallic, depth, motion vectors, light clusters, exposure, and overdraw.
- Add CI packaging for shader manifests, material compiler output, texture compression reports, capability reports, profiler JSON, and screenshots.
- Add developer command or script to regenerate shaders, material permutations, compressed textures, render goldens, and capability snapshots.

## Platform, packaging, and deployment

- Add platform capability snapshots for D3D11, D3D12, Vulkan, Metal, OpenGL, OpenGLES, and Noop/headless.
- Add platform-specific shader profile packaging and runtime lookup validation.
- Add packaging validation that required shaders, textures, materials, LUTs, probes, default assets, and metadata are present.
- Add runtime startup diagnostics for missing packaged assets, missing shader profiles, unsupported backend, and fallback backend selection.
- Add renderer config file support for preferred backend, quality tier, debug validation, shader path, asset path, and capture settings.
- Add deterministic backend selection policy for editor, runtime, CI, and headless tests.
- Add support policy documentation for each backend and feature tier.
- Add automated packaging tests for runtime executable, editor executable, shaders, default assets, and smoke scenes.
- Add CI artifacts for packaged runtime smoke and editor smoke per supported Windows backend.

## Release gates and acceptance criteria

- Define production readiness gates for renderer architecture, render graph, resources, materials, lighting, shadows, post-process, editor viewports, tests, performance, and tooling.
- Require every renderer feature to declare owner module, capability requirements, fallback behavior, runtime stats, debug view, tests, and smoke scene before being marked complete.
- Require every new render pass to declare graph resources, debug marker, CPU timing, GPU timing state, memory stats, feature gate, and tests.
- Require every new shader/material feature to declare permutation keys, binding layout, default resources, unsupported diagnostics, asset tests, and render smoke.
- Require every new render resource type to declare lifetime owner, deferred destroy behavior, memory accounting, debug name, and leak tests.
- Require every new editor viewport/render feature to include lifecycle tests and at least one multi-viewport smoke or screenshot artifact.
- Require every backend fallback to be visible in capability reports, runtime stats, logs, and debug UI.
- Require every screenshot/golden test to store backend, driver, resolution, quality tier, seed, exposure mode, and asset manifest hash.
- Add release checklist that blocks shipping on missing shader manifests, failing GPU smoke, missing default assets, resource leaks, graph validation errors, and untriaged fallbacks.

## Highest-quality milestones

- Implement GPU HDR luminance histogram and temporal auto-exposure first to close the real HDR/post-process pipeline.
- Stabilize renderer capability matrix and backend quality tiers before adding more backend-specific features.
- Build the render graph/resource lifetime foundation first: barriers, aliases, transient buffers, pass markers, and per-pass profiling.
- Add shader permutation system and material compiler before expanding advanced PBR, terrain, vegetation, and skinning variants.
- Add visual debug views early so every new renderer feature can be inspected without RenderDoc.
- Add golden screenshots and GPU smoke harness per backend/quality tier before tuning lighting and post-process.
- Add performance budgets and profiler JSON before shipping large systems such as terrain, clustered lighting, GI, and streaming.
- Implement dynamic resolution and quality scaler after profiler metrics are trustworthy.
- Expand production features in this order: lighting/shadows, IBL/probes, post-process/HDR, terrain/vegetation, skinning/animation, GI.

# Capability 03 · Runtime Gameplay / World

**Objective:** Deliver the core runtime world model for entities, components, scenes, prefabs, gameplay updates, scripting, messaging, persistence, and deterministic data-driven gameplay.

## Scene / Entity System

- Add named update phases (early, simulation, late, presentation) with a dedicated fixed-timestep phase
- Add system groups/sets with nested ordering and shared enable/disable
- Add runtime enable/disable of individual systems
- Add one-shot / run-once systems
- Add per-system persistent local state
- Add explicit sync-point insertion between phases
- Add multi-scene loading with additive and single-scene modes
- Add asynchronous scene loading with progress and completion events
- Add scene unloading with safe reference and dependency handling
- Add persistent entities that survive non-additive scene loads
- Add cross-scene entity references resolved on load
- Add sub-scene and nested-scene composition
- Add scene metadata (bounds, entity count, dependencies, thumbnail)
- Add scene bake and content preprocessing on save
- Add stable global entity identifiers for references and networking
- Add an entity reference/handle type safe across destroy and reload
- Add per-entity gameplay layers with camera culling masks
- Add sorting layers and in-layer order for overlay and 2D content
- Add exclusive entity relationships with cardinality constraints
- Add on-delete cleanup policies for relationships (cascade, orphan, block)
- Add relationship-traversal query terms (ancestors, descendants, cascade)
- Add wildcard and pair queries over relationships
- Unify typed tags and string tags behind one query-by-tag surface
- Add transform constraints (parent, aim, look-at, position/rotation lock)
- Add a spatial query acceleration structure for gameplay (nearest, overlap, ray)
- Add an entity selection and picking API for gameplay code
- Add entity groups / streaming cells addressable as sub-worlds
- Add world partitioning with load/unload of entity regions
- Add entity archetype templates distinct from prefabs
- Add bulk entity spawn/despawn with pooling
- Add entity iteration snapshots stable across structural change
- Add deterministic entity id remapping across save/load and streaming

## Component System

- Add singleton / world-resource components with get/set API
- Add cheap per-component enable/disable without archetype moves
- Add required-component declarations that auto-add dependencies
- Add component add/remove conflict validation from declared dependencies
- Add dynamic-buffer components (growable per-entity arrays)
- Add immutable shared blob data referenced by components
- Add managed component storage for reference-type payloads
- Add component pooling for churn-heavy component types
- Add chunk-level iteration and chunk-component metadata access
- Add reactive/monitor systems triggered by component changes
- Add per-field change events distinct from whole-component modified
- Add separate on-set and on-remove value semantics for observers
- Add shared-value component grouping for draw/update batching
- Add component lifecycle hooks (construct, destruct, copy, move)
- Add per-archetype component default-value templates
- Add scoped component borrow guards surfaced to gameplay code
- Add per-component-type serialization version and upgrade hooks
- Add component migration when a type layout changes
- Add component change versioning queryable from scripts

## Prefab / Instancing

- Add first-class removed-component overrides on instances
- Add per-property override lock/protection on instances
- Add configurable propagation policy for base component add/remove
- Add three-way merge and conflict reporting for concurrent asset edits
- Add nested-instance depth diagnostics and configurable limits
- Add selective refresh that preserves overridden fields
- Add an instance-to-asset apply-selected-changes workflow
- Add runtime prefab creation and saving from live entities
- Add a prefab dependency graph with reference integrity checks
- Add prefab variant diff visualization
- Add asynchronous streamed instantiation for large prefab batches
- Add prefab preview thumbnails generated on capture

## Gameplay Components / Library

- Expand the component-authoring library with reusable gameplay building blocks
- Add first-class component composition and dependency helpers to the library
- Add a component-authoring wizard and code-template generator
- Add a movement component with configurable kinematics
- Add a kinematic character-controller component with slopes, steps, and crouch
- Add steering-behavior components (seek, flee, wander, pursue, arrive)
- Add a waypoint and patrol-route component
- Add a spline-follow / path-follow component
- Add a targeting and lock-on component
- Add an attributes component set (health, stamina, custom stats) with clamping and regeneration
- Add a component-driven damage and health pipeline with mitigation and death events
- Add a stat-modifier and buff/debuff component driven by data
- Add a data-driven skill and cooldown component set
- Add a team / faction component with relationship queries (friendly, hostile, neutral)
- Add a projectile component with lifetime, homing, and impact events
- Add a weapon/emitter component with fire rate and spread
- Add a damage-zone / hazard component
- Add trigger-volume and overlap components with enter/stay/exit events
- Add a sensor/perception component (sight, hearing, proximity)
- Add an interaction component set (focus, prompt, activate)
- Add a door / switch / lever interactable component set
- Add a grab / carry / throw component set
- Add spawn-point and spawner components
- Add respawn handling as reusable components
- Add object pooling for frequently spawned entities
- Add an inventory and item-instance component model
- Add pickup, equip, and drop components
- Add a pickup-magnet and auto-collect component
- Add a tween / property-animation component
- Add a timeline/sequence player component for sequenced actions
- Add audio-trigger and one-shot sound components
- Add a footstep / surface-response component
- Add a ragdoll toggle and physics-blend component
- Add a world-space UI-attachment component (health bar, nameplate)
- Add a virtual-camera component with blends and priorities
- Add a checkpoint and level-progression component set
- Add an objective and quest tracking component set
- Add reusable local-multiplayer components for multiple local players
- Add ready-to-use gameplay component sample packages that ship with the engine

## Scripting

- Add a managed C# / .NET scripting backend
- Add an optional Python scripting backend
- Make the scripting backend registry extensible beyond the fixed set
- Add an engine-integrated coroutine scheduler with wait-for-seconds, frames, and conditions
- Add yield-aware behaviours that can suspend inside their own body
- Add async/await style operations bound to the coroutine scheduler
- Add editor-exposed property attributes (range, tooltip, header, hidden)
- Add script execution-order configuration per behaviour type
- Add script-to-script references resolved through the entity/component model
- Add serialization of script instance state for save and hot-reload
- Add a standard debug-adapter server for external IDE debugging
- Extend step debugging to the native and visual scripting backends
- Support per-instance exposed variables across all backends
- Add sandbox resource limits (instruction budget, memory ceiling, execution watchdog)
- Add per-script profiling hooks and memory accounting
- Add compile and runtime error surfacing in the editor with source locations
- Add hot-reload that preserves script instance state
- Add live tuning of exposed variables while playing
- Add a script package/module import system with versioning
- Add a scripting API versioning and deprecation policy
- Add typed auto-generated bindings from reflection metadata
- Add script templates and creation wizards
- Add a script unit-test harness and headless run mode

## Visual Scripting

- Add a node-graph authoring canvas in the editor
- Add loop nodes (for, while, for-each) with iteration guards
- Add user-defined functions, macros, and collapsed subgraphs
- Add a reusable subgraph and graph-template library
- Add a custom-node authoring SDK
- Add first-class variable get and set nodes with graph-local and shared scopes
- Add math, logic, comparison, and operator nodes
- Add literal and constant nodes
- Add type-conversion, cast, and enum/flags nodes
- Add array and map container nodes
- Add delay and latent action nodes
- Add timeline and sequence nodes
- Add a state-machine node construct with persisted state
- Add event nodes bound to the messaging system
- Add interop nodes to call and be called by text scripts
- Add live graph debugging with wire-value inspection and breakpoints
- Add step-through execution and per-node profiling
- Add node and graph versioning with migration
- Add graph-to-native compilation parity tests
- Add graph unit tests and golden-execution fixtures
- Add node search, comments, and reroute nodes for authoring ergonomics

## Event / Messaging System

- Add an engine-wide native event bus independent of the scripting runtime
- Add compile-time typed channels without string keys or value boxing
- Add subscriber priority and deterministic delivery ordering
- Add scoped RAII connection handles with automatic disconnect
- Add one-shot and auto-expiring subscriptions
- Add weak subscriptions that drop when the owner is destroyed
- Add serializable event assets bindable in the inspector
- Add local request/response (query) messaging distinct from fire-and-forget
- Add event nodes for the visual scripting backend
- Add queued events with per-channel frame draining
- Add event batching and aggregation per frame
- Add debounce and throttle policies per channel
- Add per-recipient routing filters (tag, layer, team, session)
- Add event recording, replay, and an inspectable event log
- Add a live event monitor panel in the editor
- Add back-pressure and overflow diagnostics for deferred queues

## Time / Tick / Update Loop

- Add real coroutines for scripts and native code
- Add wait-for-seconds, wait-for-frames, wait-for-event, and wait-for-load yields
- Add script-facing task creation, cancellation, and chaining
- Add a frame-pacing governor with target framerate and vsync control
- Add named user-defined tick groups and stage insertion points
- Add per-system time budgets with overrun detection
- Add adaptive tick rate for background and off-screen systems
- Add per-entity and per-layer independent clocks
- Add time-dilation zones and localized slow-motion
- Add a global pause that distinguishes gameplay, physics, and UI time
- Add async asset and scene loading with progress and yields
- Add a timeline/sequence player for scripted gameplay moments
- Add slow-frame and hitch detection with attribution
- Add a deterministic time source for replay and networking
- Add deterministic fixed-step replay of the whole simulation
- Add catch-up and interpolation controls exposed to gameplay
- Add a tick-group profiler surfaced in the editor

## Save / Load / Persistence

- Bridge live world and entity state into the save system
- Add a per-scene persistence scope and object registry
- Add a selective persistence policy per component and per entity
- Add save slots with enumeration and per-slot metadata
- Add metadata capture (name, playtime, timestamp, level, thumbnail)
- Add save thumbnails captured from the active view
- Add an autosave and checkpoint system with rotation
- Add incremental and differential saves between checkpoints
- Add asynchronous background save and load
- Add save compression
- Add tamper detection and optional encryption
- Add nested and structured save values (arrays, maps, blobs)
- Add versioned migration for world-state saves
- Add save-corruption recovery with backup slots
- Add platform cloud-save integration behind an abstraction
- Add partial and streamed saves for large worlds
- Add a save-data schema browser and inspector in the editor
- Add save/load integrity tests across version migrations

## Data-Driven Design

- Add a data-table asset with typed row structs and keyed lookup
- Add spreadsheet and CSV import into typed data tables
- Add standalone curve and gradient assets referenceable across content
- Add a shared config/data asset base for designer-authored constants
- Add enum and flag definition assets shared across content
- Add gameplay-data registries (items, abilities, stats, loot) keyed by id
- Add data inheritance and composition (base rows with overrides)
- Add cross-table references with foreign-key integrity
- Add expression/formula assets evaluated at runtime
- Add named tunables with runtime hot-reload
- Add validation and referential-integrity checks across data assets
- Add a query and filter API over data tables
- Add data baking into fast runtime lookup structures
- Add a balancing/tuning dashboard over data tables
- Add runtime-authored and player-editable data support
- Add live data-asset hot-reload into running gameplay
- Add export and diff tooling for data tables

## State Machines / Behavior / AI Logic

- Add a reusable finite state machine with states, transitions, and guards
- Add on-enter, on-update, and on-exit callbacks per state
- Add event-driven and condition-driven transitions
- Add any-state and global transitions
- Add a hierarchical state machine with nested and parallel state regions
- Add history states that resume the last active sub-state
- Add a pushdown state stack for layered states
- Add per-state timers, durations, and timeouts
- Add a visual state-machine editor
- Add serialization and visualization of active states and transitions
- Add a behavior-tree library with composite, decorator, and leaf nodes
- Add behavior-tree services and a reusable decorator library
- Add behavior-tree subtrees and shared tree assets
- Add behavior-tree live debugging and visualization
- Add a per-agent blackboard with typed keys
- Add blackboard synchronization across agents and scopes
- Add a utility-based decision system with authored utility curves
- Add a goal-oriented action planner
- Add sensor/perception feeding into decision systems
- Add cooldown and global-cooldown utility types
- Add a timer wheel for large numbers of concurrent timers
- Add a stateful visual-scripting state machine backed by the same runtime
- Add serialization of behavior-tree and planner runtime state

# Capability 04 · Terrain Editor

**Objective:** Deliver a non-destructive, scalable terrain authoring workflow that supports large worlds, procedural and manual editing, material and foliage integration, streaming, collision, navigation, validation, and production editor usability.

## Terrain Data Model

- Add a heightfield terrain resource with configurable resolution and world size
- Add a tiled/sector terrain layout for arbitrarily large terrains
- Add multi-resolution height storage with seamless tile boundaries
- Add a tile LOD hierarchy with stitched edges
- Add double-precision world coordinates for large-world terrains
- Add a signed-height and world-space coordinate mapping
- Add per-vertex and per-tile bounds with fast dirty tracking
- Add fine-grained dirty-region tracking for partial recompute
- Add a terrain hole mask for caves, tunnels, and cutouts
- Add weight/splat layers for material blending
- Add a scatter/instance layer for placed props and vegetation
- Add a spline layer for roads, rivers, and paths
- Add a metadata layer (biome id, navigation flags, physics material)
- Add per-tile compression for height, weights, and masks
- Add per-tile checksums for integrity and change detection
- Add a versioned terrain asset format with migration
- Add copy, crop, resize, and re-origin operations on terrain assets
- Add merge and split operations for terrain tiles
- Add an undo-history data model scoped to tiles and layers
- Add serialization that stores only modified tiles

## Non-Destructive Layer Stack

- Add an adjustment-layer stack applied non-destructively over the base height
- Add sculpt layers as stack entries
- Add noise layers as stack entries
- Add erosion layers as stack entries
- Add stamp layers as stack entries
- Add spline layers as stack entries
- Add per-layer masks that limit where a layer takes effect
- Add mask editing (paint, fill, invert, blur) per layer
- Add layer blend modes (add, subtract, min, max, replace, average)
- Add per-layer opacity and strength
- Add layer reorder, toggle, and solo
- Add layer rename, color-tag, and notes
- Add layer groups and folders for organisation
- Add live recompute of the stack with cached intermediate results
- Add flatten/bake of the stack into the base height on demand
- Add per-layer undo isolated from other layers
- Add copy/paste and reuse of layers across terrains
- Add a stack-cost readout so heavy layers are visible

## Sculpting Tools

- Add raise and lower sculpting
- Add smooth and relax sculpting
- Add flatten with a picked reference height
- Add a ramp tool between two picked points
- Add a terrace/step tool with adjustable step height
- Add a pinch and expand tool
- Add a twist and swirl tool
- Add a noise-add tool with selectable noise types
- Add a stamp/height-import brush from a heightmap image
- Add a clone/copy-region tool
- Add a set-absolute-height tool
- Add a slope-fill tool that builds gradients between contours
- Add a bridge tool that connects two areas smoothly
- Add a plateau and mesa tool
- Add sculpt symmetry across configurable axes
- Add sculpt masking by slope, height, and painted mask
- Add live sculpt preview before committing a stroke
- Add height and slope readout under the cursor
- Add per-tool strength curves and pressure response
- Add GPU-accelerated sculpt application for instant response

## Brush System

- Add a brush engine with radius, strength, falloff, and spacing
- Add analytic falloff curves (linear, smooth, sphere, custom curve)
- Add alpha/mask textures as brush shapes
- Add custom brush import from images
- Add a brush library with thumbnails and categories
- Add rotation and per-stamp randomisation
- Add jitter and scatter of brush stamps
- Add pen-pressure support for graphics tablets
- Add pen-tilt and rotation support
- Add brush spacing and streamline smoothing for fast strokes
- Add brush presets that bundle tool, shape, and settings
- Add on-surface brush projection that follows terrain curvature
- Add a brush-size and strength radial HUD driven by hotkeys
- Add mirror and radial-symmetry brushing
- Add a brush cursor preview that shows exact footprint
- Add lazy-mouse/stabilised strokes for steady lines

## Erosion & Simulation

- Add hydraulic erosion with rainfall, flow, and sediment transport
- Add thermal erosion with talus and slope collapse
- Add wind erosion with directional deposition
- Add coastal and shoreline erosion around water bodies
- Add sediment and debris deposition maps
- Add flow-map output for material blending
- Add wetness and moisture output
- Add GPU-accelerated erosion for interactive iteration
- Add localised erosion confined to a brushed region
- Add erosion iteration and strength controls
- Add erosion masks that protect roads, buildings, and flat areas
- Add erosion presets for common terrain styles
- Add a real-time erosion preview with adjustable strength
- Add progressive erosion that can be paused and resumed
- Add a before/after compare for erosion passes

## Material & Texture Painting

- Add layer painting onto splat weights with the brush engine
- Add a terrain material with per-layer albedo, normal, roughness, and height
- Add height-based blending between layers
- Add triplanar projection for steep slopes and cliffs
- Add per-layer tiling, rotation, and scale controls
- Add macro-variation to hide tiling repetition
- Add detail-tiling for close-up texture density
- Add auto-material rules by slope
- Add auto-material rules by height
- Add auto-material rules by curvature and flow
- Add procedural snow line and altitude tinting
- Add procedural wetness and moss on flat areas
- Add a paint mask editor with fill, clear, invert, and blur
- Add a color-tint and vertex-paint layer
- Add puddle and wetness painting tied to flow maps
- Add a material-layer palette with drag-and-drop assignment
- Add live material preview under different lighting
- Add per-layer normal-strength and specular controls
- Add layer budgets with warnings when limits are exceeded
- Add a splat-weight normalization pass to prevent artifacts

## Foliage & Scatter Painting

- Add density painting for grass, plants, rocks, and props
- Add a scatter brush with count, spacing, and randomisation
- Add an eraser and single-item placement mode
- Add placement rules by slope
- Add placement rules by height
- Add placement rules by material layer and mask
- Add per-item random scale, rotation, and tilt-to-normal
- Add avoidance rules around roads, water, and other items
- Add instance clustering for natural grouping
- Add automatic instancing and LOD for scattered items
- Add impostor fallback for distant scattered items
- Add wind and interaction response for painted foliage
- Add collision and navigation flags per scattered item type
- Add a scatter palette with weighted item sets
- Add fill-region and flood-scatter for fast coverage
- Add density heatmap visualization
- Add live count and density readouts while painting
- Add hand-off of terrain scatter into the foliage system

## Biomes & Ecosystem

- Add a biome definition combining materials, foliage, and scatter rules
- Add one-click biome painting that applies a whole ecosystem at once
- Add automatic ecosystem population from height, slope, and moisture
- Add biome blending and transition zones
- Add climate and moisture maps that drive biome distribution
- Add temperature input that shifts biome selection
- Add a biome library with ready-made presets
- Add per-biome density and variation controls
- Add biome masking and manual override painting
- Add live repopulation when the terrain shape changes
- Add biome preview and coverage visualization
- Add capture of an authored region into a reusable biome
- Add per-biome material and foliage budget checks

## Procedural Generation

- Add a node-based procedural terrain generator
- Add noise primitive nodes (perlin, simplex, ridged, billow, voronoi)
- Add gradient and shape primitive nodes
- Add combine nodes (add, multiply, blend, min, max)
- Add warp and distortion nodes
- Add remap, curve, and clamp nodes
- Add erosion and deposition nodes inside the graph
- Add slope, height, and curvature selector nodes
- Add mask and falloff nodes
- Add scatter output nodes driven by the graph
- Add material-weight output nodes driven by the graph
- Add seed control and deterministic regeneration
- Add region-limited procedural application inside a brushed area
- Add a live preview and incremental update of the graph
- Add graph presets for mountains, deserts, islands, and plains
- Add graph-to-height baking with resolution control
- Add blending of procedural output with hand-sculpted edits
- Add graph node search, comments, and reroute for authoring

## Splines, Roads & Rivers

- Add a spline tool that conforms to the terrain surface
- Add spline point insert, delete, and tangent editing
- Add road splines that flatten and carve the terrain to a profile
- Add river splines that carve channels and drive flow maps
- Add cliff and ridge splines with configurable profiles
- Add wall, fence, and border splines that place scatter along the path
- Add width, falloff, and edge-blend controls per spline
- Add automatic material assignment along splines
- Add banking and slope controls for roads
- Add intersection and junction handling between splines
- Add tunnel and bridge cutouts where splines cross terrain
- Add spline-following prop placement (guardrails, lamps, rocks)
- Add non-destructive spline layers re-evaluated on terrain edits
- Add spline snapping to existing terrain features
- Add a live preview of spline deformation before committing

## Holes, Caves & Overhangs

- Add hole cutting for cave entrances and tunnels
- Add hole-aware collision that removes geometry under cutouts
- Add hole-aware navigation
- Add optional voxel/mesh overhang regions attached to the heightfield
- Add seamless blending between heightfield and overhang meshes
- Add cave-mesh authoring tools
- Add material assignment for cave and overhang surfaces
- Add lighting and ambient-occlusion handling inside caves
- Add hole and overhang preview and validation
- Add erase and restore of previously cut holes
- Add streaming support for overhang meshes

## Stamps, Presets & Templates

- Add a stamp library of mountains, valleys, canyons, craters, and dunes
- Add drag-and-drop stamp placement with rotate and scale
- Add blend modes and falloff for stamps
- Add height-offset and clamp controls per stamp
- Add capture of a terrain region into a reusable stamp
- Add stamp thumbnails and categories
- Add starter templates for common terrain types
- Add one-click full-terrain generation from a template
- Add template parameters (size, ruggedness, water level)
- Add a community/shared stamp and preset import format
- Add favorites and recent stamps for quick access
- Add stamp preview before placement

## Water Integration

- Add lake and pond placement with automatic water level
- Add river surfaces that follow river splines
- Add an ocean/sea level with shoreline blending
- Add shoreline foam and wetness transition materials
- Add sand and mud transition zones around water
- Add water-depth-driven color and opacity
- Add flow direction and speed authoring for rivers
- Add waterfalls where rivers meet cliffs
- Add automatic terrain carving under placed water
- Add water-level preview and flood visualization
- Add underwater terrain material handling

## Import / Export & Real-World Data

- Add heightmap import in common image formats
- Add heightmap import in raw and high-bit-depth formats
- Add heightmap export
- Add real-world elevation import from standard geographic datasets
- Add georeferencing with real-world scale and coordinates
- Add satellite/aerial imagery import as a base material layer
- Add weight/splat map import and export
- Add mesh export of the terrain surface for external tools
- Add round-trip import/export that preserves layers and scatter
- Add resolution resampling on import with quality options
- Add tiled import for very large datasets
- Add import preview with automatic level and scale detection
- Add unit and coordinate-system conversion on import
- Add batch import of multiple tiles

## Streaming & Large Worlds

- Add background streaming of terrain tiles around the camera
- Add level-of-detail selection and seamless tile stitching
- Add an editor overview map for navigating large terrains
- Add per-tile edit locking and check-out for team workflows
- Add memory budgets and residency diagnostics for edited terrain
- Add partial save of only modified tiles
- Add a proxy/low-detail representation for distant tiles while editing
- Add async tile load and unload without viewport stalls
- Add a streaming radius and priority around the edit cursor
- Add tile-boundary seam validation during streaming

## Collision & Navigation

- Add automatic collision generation from the heightfield
- Add hole-aware collision that removes geometry under cutouts
- Add per-layer physics materials (friction, footstep, surface type)
- Add collision LOD separate from render LOD
- Add incremental collision rebuild limited to edited tiles
- Add automatic navigation-mesh regeneration on terrain edits
- Add navigation flags painted per region (walkable, blocked, water)
- Add collision preview and inspection overlay
- Add async collision and navigation rebuild off the main thread

## User-Friendly Authoring

- Add a guided "create terrain" wizard with size and style presets
- Add sensible smart defaults so a usable terrain exists immediately
- Add a simple tool palette with large icons and plain-language names
- Add tooltips and inline hints on every tool
- Add a short interactive tutorial for first-time users
- Add a beginner mode that hides advanced parameters
- Add real-time preview of every tool before committing
- Add always-available undo, redo, and history scrubbing
- Add a before/after compare toggle
- Add one-click biome and material application
- Add one-click auto-erosion for natural-looking terrain
- Add one-click auto-material based on slope and height
- Add draggable on-screen handles instead of numeric-only fields
- Add live suggestions ("add erosion here", "smooth this cliff")
- Add named quality presets (draft, standard, high) with no jargon
- Add auto-save and crash-safe recovery of terrain edits
- Add graphics-tablet and touch support for natural sculpting
- Add a distraction-free full-viewport sculpt mode
- Add contextual toolbars that show only relevant options
- Add friendly warnings and one-click fixes for common mistakes
- Add a gallery of example terrains that can be opened and tweaked
- Add consistent, reversible behaviour across all tools

## Editing Performance & Feedback

- Add GPU-accelerated brush application for instant response on large terrains
- Add asynchronous recompute so editing never freezes the viewport
- Add incremental updates limited to affected tiles and layers
- Add throttled auto-save that never interrupts editing
- Add a live statistics panel (size, memory, layer count, instance count)
- Add progress indicators for long operations with cancel support
- Add a performance budget with gentle warnings before limits are hit
- Add profiling of sculpt, erosion, paint, and rebuild cost
- Add background prebaking of expensive layers when idle

## Testing & Validation

- Add tile-seam continuity validation across the whole terrain
- Add height, weight, and mask range validation
- Add scatter placement determinism tests for a fixed seed
- Add import/export round-trip fidelity tests
- Add erosion reproducibility tests
- Add procedural-graph reproducibility tests
- Add undo/redo integrity tests across every tool
- Add large-terrain streaming and memory-budget stress tests
- Add golden-image tests for sculpt, paint, and material results
- Add collision and navigation regeneration correctness tests

# Capability 05 · Foliage / Vegetation / Scatter

**Objective:** Deliver a data-oriented vegetation system that supports authoring, procedural scatter, GPU-driven rendering and culling, streaming, interaction, lighting, ecosystem variation, and predictable large-world budgets.

## Foliage Data Model & Types

- Add a foliage-type asset (mesh set, materials, density, scale range, collision)
- Add grouping of foliage types into reusable palettes
- Add per-instance data (transform, scale, rotation, color, health, seed)
- Add a compact per-cell instance buffer format
- Add spatial partitioning of instances into cells
- Add weighted item sets for randomized placement
- Add per-type placement constraints (slope, height, surface, spacing)
- Add per-type render settings (LOD, shadows, wind, collision)
- Add a versioned foliage-layer asset format with migration
- Add references from foliage instances back to their source type
- Add stable instance identifiers for edit, save, and streaming
- Add per-cell bounds and checksums for change detection
- Add copy, crop, and merge operations on foliage layers

## ECS Integration & Bulk Scale

- Store foliage instances directly in archetype/chunk storage in a data-oriented layout
- Represent instance transforms and attributes as components
- Process instance chunks with hot SIMD kernels
- Add bulk spawn of millions of instances through the deferred command buffer
- Add bulk despawn through the deferred command buffer
- Add batched structural changes that avoid per-instance archetype moves
- Add parallel instance generation across the worker pool
- Add SIMD-vectorized culling kernels over instance chunks
- Add SIMD-vectorized LOD-selection kernels over instance chunks
- Add SIMD-vectorized wind kernels over instance chunks
- Add chunk-iteration queries with no per-entity overhead
- Add cache-friendly chunk layouts tuned by the chunk-size advisor
- Add zero-copy upload from instance chunks to GPU instance buffers
- Add streaming of instance chunks in and out without stalls
- Add a hybrid model where dense instances stay packed and only interactive ones become full entities
- Add lightweight instance representation that avoids full entity cost at extreme counts
- Add job-graph scheduling of scatter, culling, animation, and upload stages
- Add memory-traffic-aware batch sizes for instance processing
- Add deterministic parallel scatter stable regardless of thread count
- Add support for tens of millions of instances within fixed memory budgets
- Add near-unlimited GPU-generated grass with no per-blade CPU cost
- Add bulk transform and attribute edits applied across chunks in parallel
- Add a scale and throughput stress harness targeting extreme instance counts

## Placement & Painting Tools

- Add a paint brush that scatters instances on any surface
- Add single-instance placement with snapping and alignment
- Add an eraser with type filtering
- Add fill-region and flood placement inside a selection
- Add a lasso and polygon selection for bulk edits
- Add a replace tool that swaps one type for another
- Add adjust tools for scale on existing instances
- Add adjust tools for rotation and alignment on existing instances
- Add surface projection onto meshes
- Add surface projection onto terrain and water
- Add align-to-normal, align-to-world, and random-tilt options
- Add spacing, density, and jitter controls per stroke
- Add mask painting to limit or protect regions
- Add spline-based placement along paths and edges
- Add copy, paste, and duplicate of instance selections
- Add nudge, rotate, and scale gizmos for selections
- Add live preview of placement before committing
- Add a brush cursor that shows exact footprint and count

## Procedural Scatter Engine

- Add rule-based scatter driven by slope, height, and curvature
- Add scatter masks from painted maps
- Add density maps and moisture/climate inputs
- Add Poisson-disk and blue-noise distribution for natural spacing
- Add clustering and grouping rules for clumps and groves
- Add avoidance rules around roads, water, and structures
- Add avoidance between different foliage types
- Add layering rules (canopy, understory, ground cover) with competition
- Add ecosystem simulation that populates from environmental inputs
- Add seed control and deterministic regeneration
- Add region-limited procedural scatter inside a brushed area
- Add a node-based scatter graph
- Add live preview and incremental update of the scatter graph
- Add live repopulation when the underlying surface changes
- Add scatter presets for forests, meadows, deserts, and rocky fields
- Add scatter-result caching to avoid full regeneration

## Instanced Rendering

- Add hardware-instanced rendering of foliage meshes
- Add per-instance custom data for color, wind, and variation
- Add batching of instances by type, material, and cell
- Add indirect draw submission for large instance counts
- Add per-instance transform compression
- Add a depth-only instanced pass
- Add a shadow-only instanced pass
- Add a merged draw path for many small foliage types
- Add dynamic instance buffers updated without stalls
- Add per-instance selection and highlight for editing
- Add instance color and tint variation in the shader
- Add draw-call and instance-count stats per type

## GPU-Driven Culling & Density

- Add GPU per-instance frustum culling
- Add GPU occlusion culling for foliage instances
- Add cluster/cell culling before per-instance culling
- Add distance-based density fade with per-type cutoff
- Add screen-coverage-based instance skipping
- Add a global density scalar for quality scaling
- Add GPU compaction of visible instances into draw lists
- Add per-view culling for the main view
- Add per-view culling for shadow passes
- Add per-view culling for reflections
- Add culling debug visualization and counters
- Add a fallback CPU culling path for unsupported hardware

## Level of Detail & Impostors

- Add per-instance LOD selection by screen size and distance
- Add smooth LOD transitions with dithered cross-fade
- Add billboard fallback at distance
- Add octahedral impostor capture
- Add octahedral impostor rendering
- Add automatic impostor generation from source meshes
- Add impostor lighting that responds to scene light direction
- Add LOD bias and hysteresis to avoid popping
- Add merged distant-foliage meshes for far ranges
- Add per-type LOD distance and budget controls
- Add LOD and impostor transition debug visualization
- Add shadow LOD independent from render LOD

## Grass & Ground Cover

- Add dense grass generation from density maps
- Add GPU-generated grass blades with per-blade variation
- Add mesh-card grass option
- Add geometry-blade grass option
- Add camera-distance grass density and radius controls
- Add grass color variation from ground material and masks
- Add grass placement that follows terrain and painted surfaces
- Add grass response to wind
- Add grass response to interaction and trampling
- Add ground-cover clutter (pebbles, twigs, flowers) as cheap instances
- Add grass shadow handling tuned for density
- Add grass depth and occlusion handling
- Add grass density and coverage debug visualization

## Wind & Animation

- Add a wind source with direction, strength, and gusts
- Add wind zones with local overrides and falloff
- Add trunk-sway wind motion
- Add branch-bend wind motion
- Add leaf-flutter wind motion
- Add per-type wind stiffness and response tuning
- Add gust and turbulence noise for natural motion
- Add vertex-animation driven by wind in the foliage shader
- Add wind response scaled by instance size and age
- Add a shared wind source consumable by clouds, cloth, and particles
- Add wind debug visualization

## Interaction & Physics Response

- Add bending of grass and plants around characters
- Add bending around dynamic objects
- Add a trample/flow map that persists recent interaction
- Add per-instance push from physics bodies
- Add push response from explosions and impulses
- Add recovery and spring-back after interaction
- Add cutting and destruction states for foliage
- Add burning and scorch states for foliage
- Add interaction budgets and range limits for performance
- Add interaction debug visualization

## Lighting & Shading

- Add two-sided foliage shading
- Add translucency for thin leaves and blades
- Add subsurface scattering for foliage
- Add wind-aware normals for believable shading in motion
- Add ambient occlusion for dense foliage
- Add contact shadows for foliage
- Add distance-based shading simplification for far instances
- Add consistent lighting between meshes and their impostors
- Add seasonal and wetness tint hooks in the shader

## Seasons & Variation

- Add per-instance color and hue variation
- Add spring, summer, autumn, and winter tint sets
- Add seasonal density changes
- Add health, wilt, and dryness states driven by data
- Add snow accumulation on foliage
- Add wetness accumulation on foliage
- Add age and growth variation across instances
- Add smooth transitions when season or weather changes
- Add a shared climate/temperature input driving variation

## Streaming & Large Worlds

- Add background streaming of foliage cells around the camera
- Add load/unload of instance buffers by residency budget
- Add proxy representations for distant regions while streaming
- Add partial save of only modified foliage cells
- Add memory budgets and residency diagnostics for foliage
- Add async build of instance buffers off the main thread
- Add streaming priority around the camera and edit cursor
- Add seam handling so cells match at boundaries

## Collision & Navigation

- Add optional per-type collision (capsule, box, mesh)
- Add navigation blocking for trees and large obstacles
- Add walkable-through flags for grass and small plants
- Add collision LOD independent from render LOD
- Add automatic navigation regeneration when large foliage changes
- Add incremental collision updates limited to changed cells
- Add collision and navigation debug overlays

## Authoring UX

- Add a foliage palette with thumbnails, categories, and drag-and-drop
- Add biome brushes that paint whole vegetation sets at once
- Add ready-made presets for common environments
- Add sensible defaults so painted foliage looks good immediately
- Add real-time preview of placement
- Add always-available undo and redo
- Add plain-language controls and hover hints
- Add a beginner mode that hides advanced scatter parameters
- Add one-click "auto-populate this area" from environment rules
- Add on-screen density and count readouts while painting
- Add a distraction-free painting mode
- Add a gallery of example vegetation setups to open and tweak

## Performance & Budgets

- Add instance-count and memory budgets with gentle warnings
- Add automatic density scaling to hold a target framerate
- Add a live statistics panel (instances, draw calls, memory, culled)
- Add a quality-preset mapping (draft, standard, high) with no jargon
- Add profiling of foliage culling cost
- Add profiling of LOD and submission cost
- Add async and incremental updates so editing never stalls the viewport
- Add per-type cost attribution in the profiler

## Testing & Validation

- Add deterministic scatter tests for a fixed seed
- Add instance placement and constraint validation
- Add LOD transition tests
- Add impostor generation and rendering tests
- Add culling correctness tests across views
- Add streaming and memory-budget stress tests
- Add bulk spawn/despawn throughput tests at extreme counts
- Add undo/redo integrity tests across every tool
- Add golden-image tests for grass, wind, and impostor rendering

# Capability 06 · Sky / Atmosphere / Weather / Time-of-Day

**Objective:** Deliver an integrated atmospheric simulation and authoring system for sky, celestial bodies, clouds, weather, fog, wind, lighting, world response, audio, scripting, and scalable quality tiers.

## Sky & Atmosphere Model

- Add a physically based atmospheric-scattering sky
- Add Rayleigh scattering with configurable coefficients
- Add Mie scattering with configurable coefficients and anisotropy
- Add a sky-view lookup table for cheap full-screen sky evaluation
- Add a transmittance lookup table
- Add aerial perspective applied to distant geometry
- Add multiple-scattering approximation for realistic daylight
- Add planet curvature and configurable atmosphere height
- Add ground-albedo influence on sky and horizon color
- Add ozone absorption for accurate blue and sunset tones
- Add altitude and air-density controls
- Add a fast analytic sky option for low-end hardware
- Add a captured/HDR sky option with runtime tint and rotation
- Add automatic sky-light and ambient capture from the atmosphere
- Add horizon haze and blending into distance fog
- Add debug visualization of scattering and lookup tables

## Sun, Moon & Celestial Bodies

- Add a sun disk with adjustable size and intensity
- Add sun limb darkening
- Add a moon with orientation and surface texture
- Add moon phase computation and rendering
- Add astronomically correct sun position by date, time, and latitude
- Add astronomically correct moon position
- Add manual sun and moon positioning for art-directed skies
- Add earthshine and moonlight contribution at night
- Add support for multiple suns or custom celestial light sources
- Add solar and lunar eclipse handling
- Add a sun-glow and bloom response tied to intensity
- Add a lens-flare response for the sun

## Stars & Night Sky

- Add a star field with realistic brightness distribution
- Add a milky-way band and deep-sky background
- Add constellation and named-star placement
- Add visible planets positioned by date and time
- Add star twinkle
- Add atmospheric extinction of stars near the horizon
- Add shooting stars and meteor showers
- Add star rotation synchronized with the day/night cycle
- Add auroras for polar and stylized skies
- Add a night-sky brightness and light-pollution control

## Volumetric Clouds

- Add ray-marched volumetric clouds with layered coverage
- Add cloud types (cumulus, stratus, cirrus) driven by presets
- Add cloud coverage control
- Add cloud density and altitude controls
- Add cloud lighting with multiple scattering
- Add silver-lining and powder terms for realism
- Add cloud shadows cast onto the world
- Add wind-driven cloud movement and evolution over time
- Add weather-driven coverage that thickens before storms
- Add cheap 2D cloud-plane fallback for low-end hardware
- Add temporal reprojection to keep cloud cost low
- Add cloud-shape authoring from noise and profile curves
- Add horizon and high-altitude cloud layers
- Add quality presets scaling ray-march steps and resolution
- Add cloud rendering into reflections and distant views
- Add cloud cost budgets and diagnostics

## Time of Day

- Add a day/night cycle with adjustable day length
- Add a time-of-day value driving sun, moon, sky, and lighting
- Add pause, scrub, and playback-speed control of time
- Add date, season, and latitude inputs
- Add keyframed sky and lighting profiles across the day
- Add smooth interpolation between time-of-day keyframes
- Add sunrise, noon, sunset, and night presets
- Add golden-hour and blue-hour tuning
- Add season-driven sun path and day-length changes
- Add scripting hooks for time events (dawn, dusk, midnight)
- Add save and restore of the current time state
- Add a time-of-day timeline editor with a 24-hour track

## Weather System

- Add a weather-state model (clear, cloudy, rain, storm, snow, fog)
- Add smooth transitions and blending between weather states
- Add a weather timeline and scheduler
- Add randomized and seeded weather sequences
- Add localized weather zones with falloff
- Add intensity control per weather state
- Add storm build-up with darkening sky and rising wind
- Add lightning generation
- Add thunder with distance-based delay
- Add weather presets and a preset blending system
- Add gameplay and scripting hooks for weather changes
- Add save and restore of the current weather state
- Add climate profiles that bias weather probability by region
- Add deterministic weather for replays and networked sessions

## Precipitation & Accumulation

- Add rain with adjustable density, speed, and angle
- Add snow with drift and settling behavior
- Add hail and sleet variants
- Add rain splashes and ripples on surfaces
- Add rain interaction with water surfaces
- Add camera-relative precipitation that follows the view
- Add occlusion so precipitation stops under cover
- Add surface wetness that builds and dries over time
- Add puddle formation in low areas during rain
- Add snow accumulation on upward-facing surfaces
- Add ice and frost formation in cold conditions
- Add gradual melt and evaporation as weather clears
- Add wind influence on precipitation direction
- Add precipitation particle budgets and quality scaling

## Fog & Atmospheric Effects

- Add exponential height fog with color and density controls
- Add volumetric fog with light scattering
- Add light shafts and god rays from the sun
- Add light shafts from local lights
- Add ground mist and low-lying fog banks
- Add fog color driven by time of day and sky
- Add distance and depth fog blended with aerial perspective
- Add localized fog volumes with falloff
- Add heat-haze and shimmer effects
- Add fog quality scaling and cost budgets

## Lighting Integration

- Drive the main directional light from the sun position
- Drive a secondary directional light from the moon at night
- Update sky-light and ambient from the current atmosphere on change
- Add color-temperature shifts across the day
- Add automatic exposure adaptation across day and night
- Add cloud and weather dimming of direct light
- Add lightning flashes as transient scene lighting
- Update global illumination when the sky changes significantly
- Add night artificial-light response (streetlights on at dusk)
- Add shadow color and softness tied to sky conditions
- Add throttled sky-light updates to control cost

## Wind Integration

- Add a global wind driven by weather and time
- Add gusts and turbulence that ramp with storms
- Add wind direction changes over time
- Add wind zones with local overrides
- Expose wind to foliage from one shared source
- Expose wind to cloth and particles from the same source
- Expose wind to clouds from the same source
- Add wind strength visualization and debug readout

## Weather Effects on the World

- Add dynamic surface wetness response in materials
- Add snow material response tied to accumulation
- Add ice material response tied to freezing
- Add puddle reflections and ripple response
- Add lightning strike points with world impact and light
- Add wind-driven debris and leaves during storms
- Add temperature as a shared value driving snow, ice, and melt
- Add weather influence on foliage color and health

## Audio Integration

- Add ambient rain and storm soundscapes
- Add ambient wind soundscapes
- Add thunder synchronized with lightning and distance
- Add smooth audio transitions between weather states
- Add interior and sheltered attenuation of weather audio
- Add time-of-day ambience (birds at dawn, crickets at night)
- Add audio intensity tied to weather strength

## Authoring & Presets

- Add a time-of-day keyframe editor with a day timeline
- Add a sky and atmosphere preset library
- Add a weather preset library with blend weights
- Add curve-based control of color over time
- Add curve-based control of intensity and density over time
- Add climate presets that bundle sky, weather, and lighting
- Add capture of the current look into a reusable preset
- Add layering of art-directed overrides on top of physical simulation
- Add copy and share of sky and weather presets
- Add preset thumbnails and categories

## User-Friendly Authoring

- Add a simple time-of-day slider with live preview
- Add one-click weather buttons (clear, rain, storm, snow, fog)
- Add plain-language sliders (cloudiness, wind, wetness, warmth)
- Add sensible defaults that produce a good sky immediately
- Add a beginner mode that hides physical parameters
- Add "make it dramatic" and "make it calm" one-click looks
- Add a scrubbable day preview to see the sky across 24 hours
- Add before/after compare for preset changes
- Add hover hints and a short guided tour
- Add a gallery of example skies and weather to open and tweak
- Add always-available undo and redo for every change

## Data-Driven & Scripting

- Add a weather-schedule asset for scripted campaigns
- Add events for weather changes consumable by gameplay
- Add events for time-of-day milestones consumable by gameplay
- Add a scripting API to query and set time
- Add a scripting API to query and set weather and wind
- Add conditional weather triggered by location or gameplay state
- Add deterministic weather for replays and networked sessions
- Add persistence of full sky and weather state in saves

## Performance & Quality Scaling

- Add quality presets scaling sky, cloud, and fog cost
- Add lookup-table caching for sky and aerial perspective
- Add temporal amortization of expensive atmosphere work
- Add temporal amortization of expensive cloud work
- Add resolution scaling for volumetric passes
- Add budgets and diagnostics for sky, cloud, and weather cost
- Add automatic downscaling to hold target framerate

## Testing & Validation

- Add golden-image tests across representative times of day
- Add golden-image tests across weather states
- Add weather-transition determinism tests
- Add sky lookup-table validation
- Add sun-position accuracy tests by time, date, and latitude
- Add moon-phase and moon-position accuracy tests
- Add save/restore fidelity tests for sky and weather state
- Add performance budget tests for volumetric passes

# Capability 07 · Water / Ocean

**Objective:** Deliver authorable oceans, lakes, rivers, and waterfalls with scalable simulation, rendering, shoreline and terrain integration, underwater presentation, physics, navigation, weather response, diagnostics, and quality controls.

## Water Data Model & Body Types

- Add a water-body asset with type, bounds, level, and material reference
- Add body types (ocean, sea, lake, pond, river, stream, pool, custom volume)
- Add a water-volume representation for buoyancy and containment
- Add per-body surface level and depth field
- Add a shared water material with per-body overrides
- Add per-body simulation settings (wave scale, flow, calmness)
- Add a spline definition for rivers and shaped water
- Add a mask layer that limits where a body renders
- Add tiling and infinite-extent flags for oceans
- Add per-body bounds, checksums, and dirty tracking
- Add a versioned water asset format with migration
- Add copy, crop, and merge operations on water bodies
- Add references from water bodies to terrain tiles they touch

## One-Click Water Creation

- Add a one-click ocean that fills the world to a chosen level
- Add a one-click sea bounded to a region
- Add a one-click lake that auto-fills a terrain basin
- Add a one-click pond placed under the cursor
- Add a one-click pool with straight edges and a fixed level
- Add a one-click river started by drawing a spline
- Add automatic water-level detection from surrounding terrain
- Add automatic basin detection and flood-fill for lakes
- Add drag-to-place sizing with live preview
- Add sensible defaults so new water looks good immediately
- Add a water-type picker with clear icons and names
- Add per-type presets (calm lake, rough sea, tropical ocean, mountain stream)
- Add automatic shoreline blending on creation
- Add a guided create-water wizard for first-time users

## Spline Water Authoring

- Add a river spline tool that follows and carves the terrain
- Add spline point insert, delete, and tangent editing
- Add a width profile along the spline
- Add a depth profile along the spline
- Add automatic flow direction derived from spline and slope
- Add flow-speed control along the spline
- Add bank blending and shoreline width per segment
- Add meander and natural-curve smoothing helpers
- Add branching and confluence handling where rivers merge
- Add automatic waterfalls where the spline crosses cliffs
- Add rapids and turbulence zones on steep segments
- Add automatic terrain carving under the river channel
- Add snapping of river ends to lakes, seas, and other rivers
- Add spline-following foam, debris, and rocks
- Add non-destructive spline re-evaluation when terrain changes
- Add a live preview of the river before committing

## Ocean Simulation

- Add spectral ocean simulation from a wave spectrum
- Add a configurable wave spectrum (wind speed, fetch, direction)
- Add multi-cascade waves covering large and small scales
- Add Gerstner-wave synthesis for sharp crests
- Add choppiness and crest-sharpening controls
- Add wind-driven wave direction and alignment
- Add swell layered on top of local wind waves
- Add a sea-state scale from calm to storm
- Add GPU evaluation of the wave displacement and normals
- Add foam generation from wave steepness and folding
- Add per-cascade tiling and detail controls
- Add wave height, slope, and Jacobian sampling on the CPU for gameplay
- Add deterministic ocean state for replays and networking
- Add smooth transitions between sea states
- Add ocean simulation cost budgets and quality scaling

## Waves (Lakes, Rivers & Ambient)

- Add Gerstner waves for lakes and enclosed water
- Add ambient ripple detail from noise
- Add wind-driven ripple direction and strength
- Add wave damping near shorelines and in sheltered areas
- Add fetch-based wave scaling by open-water distance
- Add river surface chop tied to flow speed
- Add small-scale surface detail normals
- Add CPU height sampling for enclosed-water gameplay

## Water Surface Rendering

- Add depth-based water color and absorption
- Add transparency and Fresnel-driven reflectivity
- Add screen-space reflections for the water surface
- Add planar reflections as a higher-quality option
- Add reflection-probe fallback where screen data is missing
- Add refraction with depth-aware distortion
- Add animated normal maps blended across scales
- Add detail normals for close-up surface richness
- Add sun and moon specular glitter
- Add subsurface scattering for shallow and backlit water
- Add sky and cloud reflection tied to time of day
- Add edge softening where water meets geometry
- Add a distance detail fade to hide tiling
- Add color presets (tropical, murky, glacial, swamp)
- Add a live surface preview under different lighting

## Foam & Whitecaps

- Add whitecap foam from wave crests
- Add shoreline foam that follows the waterline
- Add foam around objects intersecting the surface
- Add foam from flow turbulence and rapids
- Add foam from waterfall impact points
- Add foam texture blending and animation
- Add foam persistence and dissipation over time
- Add foam density and coverage controls
- Add foam response to wind strength
- Add a foam mask for hand-painted control

## Shoreline & Terrain Blending

- Add depth-based shoreline transition
- Add wet-sand and wet-rock material response near the waterline
- Add an animated foam line at the shore
- Add wave run-up and swash on beaches
- Add automatic shoreline detection from terrain height
- Add soft intersection blending to hide hard edges
- Add tide level with slow rise and fall
- Add shoreline debris and seaweed placement
- Add underwater terrain darkening and color grading
- Add automatic terrain wetness zone around water bodies

## Underwater Rendering

- Add underwater fog with depth-based color and density
- Add underwater caustics on submerged surfaces
- Add underwater god rays from the sun
- Add surface-from-below rendering with total internal reflection
- Add screen distortion and blur underwater
- Add rising bubbles and particulate matter
- Add depth-based murkiness and visibility falloff
- Add a smooth camera transition crossing the surface
- Add underwater ambient audio handoff

## Flow & Currents

- Add flow maps driving surface motion
- Add river flow derived from spline and slope
- Add directional ocean currents
- Add flow-driven foam and debris transport
- Add whirlpools and vortex zones
- Add rapids with increased turbulence and foam
- Add flow authoring and painting tools
- Add flow visualization overlay
- Add flow forces applied to floating objects

## Water Interaction & Dynamics

- Add dynamic ripples from objects touching the surface
- Add wakes trailing moving objects
- Add splashes on impact and exit
- Add an interactive height-field simulation for local disturbances
- Add ripple propagation and reflection off shores
- Add object-size-scaled disturbance strength
- Add persistent trails that fade over time
- Add interaction budgets and active-region limits
- Add coupling between the interactive sim and the base waves
- Add interaction debug visualization

## Buoyancy & Physics

- Add a water-height sampling API for gameplay and physics
- Add buoyancy forces for floating objects
- Add submersion-depth and displaced-volume computation
- Add water drag and damping on submerged bodies
- Add current and flow forces pushing objects
- Add multi-point buoyancy sampling for stable floating
- Add wave-driven bobbing and rocking of floating objects
- Add a swimmable-volume query for characters
- Add enter-water and exit-water physics events
- Add buoyancy determinism for replays and networking

## Caustics

- Add projected animated caustics from the surface
- Add depth-aware caustics that fade with distance
- Add caustics on terrain and submerged objects
- Add sun and moon direction driving caustic projection
- Add caustic intensity tied to water clarity
- Add caustics above water from shallow ripples
- Add caustics quality scaling and budgets

## Waterfalls & Rapids

- Add waterfall meshes generated from river cliffs
- Add falling-water particles and streaks
- Add spray and mist at the base
- Add foam and turbulence at the impact pool
- Add waterfall lighting and translucency
- Add wetness on rocks behind and beside the fall
- Add waterfall audio handoff
- Add rapids surface treatment on steep river segments

## Wetness & Splash Response

- Add surfaces becoming wet near and after contact with water
- Add splash decals on nearby surfaces
- Add drip and runoff after leaving water
- Add a wet-to-dry transition over time
- Add shared wetness input with the weather system
- Add character and object wet-material response

## Weather & Sky Integration

- Drive ocean wave height from wind strength
- Add rain ripples on the water surface
- Add storm sea-state escalation
- Reflect sky, clouds, and time-of-day color on the surface
- Add freezing to ice in cold conditions
- Add thaw from ice back to open water
- Add calm-to-storm and storm-to-calm transitions
- Add wind direction alignment of waves and foam
- Add shared wind and temperature inputs from the sky system

## Level of Detail & Tiling

- Add a projected-grid or clipmap water mesh
- Add distance-based surface LOD
- Add infinite ocean tiling without visible seams
- Add detail and foam fade with distance
- Add streaming of large water regions
- Add a low-detail distant-water representation
- Add LOD transition smoothing to avoid popping

## Collision & Navigation

- Add water-volume triggers for enter and exit
- Add swimmable-region tagging for navigation
- Add navigation flags for water-blocked and swim areas
- Add depth zones (shallow, wadeable, deep)
- Add flow-affected navigation cost
- Add collision handoff for solid ice surfaces

## Data-Driven & Scripting

- Add a scripting API to query water height, depth, and flow
- Add enter-water and exit-water gameplay events
- Add flow and current trigger volumes
- Add a sea-state schedule asset
- Add conditional water state from gameplay and weather
- Add deterministic water for replays and networked sessions
- Add persistence of water and sea state in saves

## Authoring UX

- Add a water palette with body types, materials, and presets
- Add plain-language sliders (calmness, wave height, murkiness, flow)
- Add real-time preview of every change
- Add always-available undo and redo
- Add draggable on-screen handles for level, size, and flow
- Add a beginner mode that hides simulation parameters
- Add one-click looks (calm, choppy, stormy, tropical, murky)
- Add hover hints and a short guided tour
- Add on-screen readouts of depth and flow under the cursor
- Add a gallery of example water setups to open and tweak
- Add contextual toolbars per water tool

## Performance & Quality Scaling

- Add quality presets scaling simulation and rendering cost
- Add simulation-resolution scaling for waves and interaction
- Add reflection-quality scaling
- Add temporal amortization of reflections and caustics
- Add budgets and diagnostics for water cost
- Add automatic downscaling to hold target framerate
- Add async simulation off the main thread

## Testing & Validation

- Add golden-image tests across calm, choppy, and storm states
- Add water-height sampling accuracy tests
- Add buoyancy determinism tests
- Add ocean-tiling seam-continuity tests
- Add flow and current reproducibility tests
- Add shoreline-blending correctness tests
- Add save/restore fidelity tests for water and sea state
- Add performance budget tests for simulation and reflections

# Capability 08 · World Streaming / World Partitioning / Open World

**Objective:** Deliver deterministic large-world partitioning and streaming with bounded memory and I/O, stable persistence, origin management, editor tooling, multiplayer coordination, diagnostics, and seamless runtime traversal.

## Automatic World Partitioning & Grid

- Add automatic spatial partitioning of the world into streaming cells
- Add a runtime spatial hash grid for fast cell lookup
- Add automatic assignment of placed content to cells by position
- Add configurable cell size with a sensible automatic default
- Add multiple overlapping grids for different content classes
- Add cell bounds computed from contained content
- Add loose cells for oversized objects that span boundaries
- Add always-loaded content that is never streamed out
- Add automatic re-partitioning when content moves between cells
- Add incremental background partition rebuild
- Add per-cell content manifests generated on save
- Add spatial queries returning the cells overlapping a region
- Add deterministic cell identifiers stable across edits
- Add nested and multi-resolution grids for varied content density

## Automatic Streaming

- Add distance-based automatic streaming with zero manual setup
- Add load of cells entering the streaming radius
- Add unload of cells leaving the streaming radius
- Add hysteresis bands to avoid load/unload thrashing at boundaries
- Add prefetch of cells in the direction of movement
- Add priority ordering by distance and view direction
- Add streaming enabled by default for new worlds
- Add per-cell load and unload lifecycle events
- Add smooth activation so content appears without a pop
- Add a global streaming on/off toggle for debugging
- Add automatic tuning of the streaming radius from performance headroom
- Add graceful handling when content loads slower than movement
- Add configurable per-content-class streaming distances

## Streaming Sources

- Add the camera as a default streaming source
- Add player and character streaming sources
- Add gameplay-defined streaming sources (objectives, spawns)
- Add per-source streaming radius and priority
- Add velocity-based predictive streaming per source
- Add multiple simultaneous sources for split-screen and multiplayer
- Add temporary streaming sources for teleport and fast travel
- Add source shapes (sphere, box, along-spline) for shaped streaming
- Add merging and deduplication of overlapping sources
- Add priority boosting for the local player source
- Add streaming-source debug visualization

## Origin Rebasing & Large Coordinates

- Add a floating-origin system that recenters the world near the camera
- Add automatic origin shift when the camera exceeds a threshold
- Add rebasing of rendering transforms on origin shift
- Add rebasing of physics bodies on origin shift
- Add rebasing of audio positions on origin shift
- Add rebasing of particles and effects on origin shift
- Add double-precision world coordinates for authoritative positions
- Add conversion between world-space and rebased render-space
- Add seamless origin shifts with no visible jump
- Add per-view local origins for multiplayer and split-screen
- Add precision diagnostics that warn before coordinate error grows
- Add gameplay-code helpers that hide rebasing from designers
- Add rebasing hooks for streamed navigation and spatial structures
- Add rebasing of shadows, reflections, and large-scale effects

## Cell Loading Pipeline

- Add fully asynchronous cell loading off the main thread
- Add background IO for cell data
- Add background deserialization of cell content
- Add staged GPU upload of streamed meshes and textures
- Add job-system integration for parallel cell loads
- Add incremental activation spread across frames to avoid hitches
- Add cancellation of in-flight loads when a cell leaves range
- Add load coalescing when a cell is requested multiple times
- Add retry and error handling for failed cell loads
- Add placeholder or proxy display while a cell finishes loading
- Add load-completion callbacks and events
- Add ordering so dependencies load before dependents

## Hierarchical Distant Proxies

- Add hierarchical proxy meshes for cells beyond the streaming radius
- Add automatic generation of distant proxies from cell content
- Add merged-mesh proxies for static content
- Add impostor proxies for very distant content
- Add multi-level proxy hierarchies for continuous distance coverage
- Add smooth transitions between loaded content and proxies
- Add automatic proxy rebuild when cell content changes
- Add proxy material and lighting matching to loaded content
- Add per-cluster proxy grouping to bound draw counts
- Add proxy budgets and quality scaling
- Add proxy debug visualization
- Add incremental background proxy baking

## Logical Content Layers

- Add logical content layers independent of spatial cells
- Add runtime enable and disable of content layers
- Add layer variants (day/night, pre/post event, difficulty)
- Add per-layer streaming policy (always, distance, never)
- Add layer states that swap sets of content
- Add gameplay-driven layer activation
- Add save and restore of active layer states
- Add editor authoring and assignment of content to layers
- Add layer visualization and isolation in the editor
- Add validation that layer references resolve
- Add nested layers and layer groups

## Per-Object Files & Team Workflow

- Add one-file-per-object storage for streamed content
- Add automatic file placement by cell and layer
- Add granular files that avoid source-control merge conflicts
- Add lazy loading of individual object files
- Add batch packing of object files for shipping builds
- Add rename and move handling that preserves references
- Add per-object dirty tracking for partial saves
- Add integrity checks across object files in a cell
- Add a migration path from monolithic to per-object storage
- Add conflict-free concurrent editing of different cells
- Add cross-object reference resolution across files

## Instanced Level Chunks

- Add reusable level chunks that can be instanced across the world
- Add per-instance overrides on level chunks
- Add streaming of instanced chunks by distance
- Add nested chunks within chunks
- Add stable references into instanced chunk content
- Add spawn and despawn of chunk instances at runtime
- Add chunk-instance registries and lookup
- Add editor placement and editing of chunk instances
- Add validation for cyclic chunk nesting
- Add chunk-instance save and restore
- Add automatic cell assignment for chunk instances

## ECS Integration & Bulk Streaming

- Stream world content as archetype/chunk storage
- Add bulk load of entities through the deferred command buffer
- Add bulk unload and recycling of streamed entities
- Add chunked serialization and deserialization of streamed cells
- Add delta snapshots for streamed cell state
- Add streaming visitors that process cells without full loads
- Add parallel cell load and build across the worker pool
- Add zero-copy handoff of streamed data into archetype storage
- Add stable entity identity across unload and reload
- Add deterministic entity id remapping on stream-in
- Add memory-traffic-aware batch sizes for stream-in
- Add streaming of singleton and world-resource state
- Add SIMD-friendly layouts preserved across streaming

## Streaming State & Persistence

- Add persistence of modified streamed entities when they unload
- Add an offloaded state store for unloaded cells
- Add dirty tracking so only changed content is persisted
- Add restore of persisted state when a cell reloads
- Add distinction between authored content and runtime changes
- Add save integration that captures streamed and unloaded state
- Add compaction of the offloaded state store
- Add versioned migration of persisted streamed state
- Add per-object persistence policy (persistent, resettable)
- Add reset of a region to its authored state on demand
- Add async flush of persisted state without stalls

## Seamless World Travel

- Add seamless movement across the whole world with no loading screens
- Add portal and doorway handoff between regions
- Add fast-travel that pre-streams the destination
- Add teleport that relocates streaming sources and origin
- Add background pre-streaming during scripted sequences
- Add hidden-loading corridors for tight interiors
- Add smooth handoff between interior and exterior streaming
- Add a fallback loading transition when streaming cannot keep up
- Add continuity of audio and lighting across transitions
- Add validation that no traversal path outruns streaming
- Add pre-streaming from predicted player intent

## Navigation & Physics Streaming

- Add streamed navigation-mesh tiles per cell
- Add stitching of navigation tiles at cell boundaries
- Add streamed collision per cell
- Add physics activation and sleep per streamed region
- Add async navigation and collision build on stream-in
- Add rebasing of navigation and physics on origin shift
- Add navigation queries that trigger streaming of needed tiles
- Add fallback navigation for not-yet-streamed regions
- Add validation of navigation continuity across cells
- Add debug visualization of streamed navigation and collision
- Add long-range path planning across unloaded regions

## Audio & Ambient Streaming

- Add streamed audio and ambience zones per region
- Add reverb and acoustic settings streamed per area
- Add crossfade of ambience across region boundaries
- Add distance-based streaming of audio sources
- Add rebasing of audio positions on origin shift
- Add pre-streaming of audio for fast travel and teleport
- Add budgets for concurrently streamed audio
- Add validation of audio-zone coverage and gaps
- Add debug visualization of audio zones
- Add handoff of interior and exterior audio

## Streaming Budgets & Throttling

- Add a memory budget for loaded cells with eviction
- Add a frame-time budget for activation work
- Add a bandwidth budget for background IO
- Add priority queues for load and unload requests
- Add adaptive throttling from current performance headroom
- Add pinned cells exempt from eviction
- Add an eviction policy by distance, recency, and priority
- Add over-budget diagnostics with responsible cells
- Add graceful degradation to proxies when over budget
- Add per-content-class budgets (meshes, textures, entities)
- Add spike smoothing so large regions load progressively

## Automatic Setup & User-Friendly UX

- Make world streaming on by default with zero configuration
- Add automatic cell-size selection from world scale and content density
- Add automatic streaming-radius selection from performance
- Add a single toggle to enable or disable streaming
- Add plain-language presets (small, large, huge open world)
- Add sensible defaults that just work for new projects
- Add automatic conversion of an existing world into a streamed world
- Add guidance and warnings when content is misconfigured for streaming
- Add a one-click "optimize streaming" analysis and fix
- Add a beginner mode that hides partition internals
- Add clear, non-technical status readouts (loaded, loading, budget)
- Add automatic always-loaded detection for critical systems
- Add an assistant that suggests fixes for streaming problems

## Editor Tools & Visualization

- Add a partition grid overlay in the editor
- Add loaded, loading, and unloaded cell visualization
- Add a world overview map with cell states
- Add manual pin and force-load of cells for editing
- Add per-cell content and memory statistics
- Add a streaming-source preview and radius display
- Add simulate-streaming-from-here in the editor
- Add content-to-cell assignment inspection
- Add layer isolation and toggling in the editor
- Add a proxy vs full-content compare view
- Add jump-to-cell navigation
- Add warnings for content spanning too many cells

## Large Tiled Worlds

- Add large tiled worlds composed of many regions
- Add per-tile bounds and position in world space
- Add distance-based tile streaming
- Add tiled terrain and content aligned to the partition
- Add a world overview for composing and arranging tiles
- Add automatic alignment and seam matching between tiles
- Add per-tile origin offsets for large coordinate ranges
- Add import and assembly of tiles into one world
- Add tile-level enable/disable and variants
- Add validation of tile coverage and overlaps

## Networking & Multiplayer Streaming

- Add server-authoritative streaming decisions
- Add per-client relevance and streaming radius
- Add replication of streamed and offloaded state
- Add per-client independent origins and rebasing
- Add consistent cell identity across server and clients
- Add prioritized streaming around each connected player
- Add spawn and despawn replication tied to cell lifecycle
- Add bandwidth-aware streaming for networked sessions
- Add deterministic streaming for lockstep and replay
- Add validation of client and server streamed-state consistency

## Performance & Diagnostics

- Add streaming statistics (loaded cells, pending, memory, bandwidth)
- Add hitch detection attributed to streaming work
- Add per-cell load-time profiling
- Add residency and eviction telemetry
- Add a streaming timeline for load and unload events
- Add memory-budget and over-budget reporting
- Add warnings when streaming cannot keep up with movement
- Add a headless streaming benchmark harness
- Add machine-readable streaming metrics for CI
- Add a live streaming HUD for profiling

## Testing & Validation

- Add streaming determinism tests for a fixed traversal path
- Add origin-rebasing correctness and precision tests
- Add cell load and unload lifecycle tests
- Add seam-continuity tests across cell boundaries
- Add persisted-state save and restore fidelity tests
- Add memory-budget and eviction stress tests
- Add fast-travel and teleport pre-streaming tests
- Add navigation and physics streaming continuity tests
- Add proxy-to-content transition tests
- Add a large-world traversal soak test

# Capability 09 · LOD Management

**Objective:** Deliver unified level-of-detail generation, selection, transition, streaming, authoring, and budgeting across meshes, terrain, foliage, animation, materials, and hierarchical world representations.

## LOD System Core & Data Model

- Add a level-of-detail chain per mesh with ordered levels
- Add per-level mesh geometry references
- Add per-level material assignments
- Add screen-size thresholds per level
- Add distance thresholds as an alternative selection metric
- Add per-level bounds and error metadata
- Add a shared LOD-settings asset reusable across meshes
- Add a lowest-level fallback (billboard or single quad)
- Add per-level triangle and vertex counts stored for budgeting
- Add a versioned LOD asset format with migration
- Add LOD data shared between mesh instances
- Add validation that LOD chains are complete and monotonic
- Add a unified LOD interface across meshes, terrain, and instances

## Automatic Mesh LOD Generation

- Add automatic mesh simplification generating LOD levels
- Add target-triangle-count reduction
- Add target-percentage reduction
- Add a view-independent geometric error metric
- Add silhouette and boundary preservation
- Add UV-seam preservation
- Add normal and hard-edge preservation
- Add vertex-color and attribute preservation
- Add skinning-weight preservation for skinned meshes
- Add material-boundary preservation during reduction
- Add automatic material and texture reduction at lower levels
- Add automatic LOD generation on asset import
- Add per-level reduction overrides
- Add regeneration when the source mesh changes
- Add batch LOD generation across many assets
- Add reduction quality presets

## LOD Selection & Switching

- Add screen-coverage-based LOD selection
- Add distance-based LOD selection
- Add hysteresis to prevent switching flicker at thresholds
- Add a global LOD bias control
- Add per-object LOD bias and forced LOD
- Add per-view LOD selection (main, shadow, reflection)
- Add GPU-side LOD selection for instanced content
- Add field-of-view-aware screen-size computation
- Add resolution-aware selection so LOD scales with output size
- Add priority so important objects hold higher LOD
- Add SIMD-vectorized LOD selection over instance chunks
- Add per-object selection debug output
- Add deterministic selection for replay and tests

## LOD Transitions & Blending

- Add dithered cross-fade between LOD levels
- Add alpha-blended transitions as an option
- Add geometric vertex morphing between levels
- Add screen-door transition for opaque content
- Add temporal transitions spread across frames
- Add transition distance and duration controls
- Add pop-free switching validation
- Add transition suppression for fast-moving or distant objects
- Add overdraw control during blended transitions
- Add per-object transition-style overrides
- Add transition debug visualization

## Impostors & Billboards

- Add octahedral impostor capture for distant meshes
- Add octahedral impostor rendering
- Add billboard and cross-quad far LOD
- Add automatic impostor generation from source meshes
- Add impostor atlas packing
- Add impostor lighting that responds to scene light direction
- Add impostor normal and depth capture for parallax
- Add impostor as the automatic last LOD level
- Add impostor resolution and quality controls
- Add smooth blend from mesh LOD to impostor
- Add impostor regeneration when the mesh changes
- Add impostor memory budgets

## Virtualized Cluster Geometry

- Add cluster-based continuous geometry LOD
- Add automatic cluster-hierarchy generation from source meshes
- Add per-cluster bounds and error for selection
- Add view-dependent cluster LOD selection at pixel scale
- Add GPU per-cluster culling and LOD
- Add streaming of geometry clusters by need
- Add seamless cluster-boundary LOD without cracks
- Add material support across clustered geometry
- Add shadow rendering from clustered geometry
- Add a fallback traditional-LOD path for unsupported hardware
- Add memory and streaming budgets for clustered geometry
- Add cluster-level statistics and diagnostics
- Add an import and build pipeline for clustered assets
- Add quality scaling of cluster detail

## Terrain LOD

- Add continuous level-of-detail for the terrain surface
- Add a quadtree tile LOD hierarchy
- Add a clipmap-style camera-relative terrain LOD
- Add screen-space geometric error driving tile subdivision
- Add geomorphing between terrain LOD levels
- Add seamless stitching between adjacent tile LODs
- Add skirts or edge fans to hide LOD cracks
- Add tessellation-based terrain LOD where supported
- Add a mesh-LOD fallback where tessellation is unavailable
- Add height-based detail refinement near the camera
- Add per-tile LOD budgets and triangle limits
- Add hole-aware terrain LOD
- Add collision LOD separate from render LOD for terrain
- Add terrain LOD transitions without visible popping
- Add terrain LOD debug visualization (rings, tile levels)
- Add deterministic terrain LOD for tests

## Terrain Material & Detail LOD

- Add distance-based blending of terrain material layers
- Add macro-material substitution at far distance
- Add splat-weight LOD to reduce sampling far away
- Add normal-map and detail-map fade with distance
- Add a triplanar-to-simple projection switch at distance
- Add procedural detail meshes (rocks, tufts) with their own LOD
- Add layer-count reduction at lower detail
- Add far-distance baked color and normal for terrain
- Add a seamless transition between detail and macro material
- Add terrain material LOD quality presets

## Hierarchical Merged LOD

- Add merging of groups of static meshes into combined distant proxies
- Add automatic generation of merged proxies
- Add material atlasing for merged proxies
- Add multi-level merged hierarchies for continuous coverage
- Add cluster grouping to bound proxy draw counts
- Add automatic proxy rebuild when source content changes
- Add smooth transition between individual meshes and merged proxies
- Add proxy generation as an offline or background bake
- Add integration with world streaming for distant regions
- Add merged-proxy budgets and diagnostics
- Add merged-proxy debug visualization

## Skinned & Animated LOD

- Add geometry LOD for skinned meshes
- Add bone-count reduction per LOD level
- Add animation update-rate reduction with distance
- Add skinning-quality reduction at lower levels
- Add morph-target reduction per LOD
- Add off-screen and distant animation pausing
- Add interpolation to hide reduced update rates
- Add crowd-friendly aggressive LOD for many characters
- Add per-character LOD bias and forced LOD
- Add validation of skinning correctness across LODs
- Add skinned-LOD debug visualization

## Instanced & Foliage LOD Management

- Add centrally managed per-instance LOD selection
- Add density reduction as a distance LOD for instances
- Add merged distant meshes for instanced content
- Add impostor last-LOD for instances
- Add SIMD-vectorized instance LOD selection
- Add per-instance-type LOD distances and budgets
- Add shadow LOD independent from render LOD for instances
- Add cross-fade transitions for instanced LOD
- Add GPU-driven instance LOD selection
- Add unified LOD settings shared with the foliage system
- Add instance LOD debug visualization

## Material & Shader LOD

- Add shader-complexity reduction at distance
- Add feature dropping far away (parallax, detail layers, clearcoat)
- Add texture-mip-driven material simplification
- Add a reduced lighting model for distant objects
- Add material-layer-count reduction at lower LODs
- Add a cheap far-distance material variant per material
- Add automatic shader-LOD variant generation
- Add per-quality-tier material LOD mapping
- Add validation that material LODs match visually
- Add material-LOD debug visualization

## LOD Streaming

- Add on-demand streaming of individual LOD levels
- Add loading of higher detail as objects approach
- Add eviction of unused high-detail LODs under memory pressure
- Add per-LOD memory budgets
- Add prioritized LOD streaming by screen coverage
- Add placeholder lower-LOD display while higher LOD loads
- Add async LOD load off the main thread
- Add coalescing of duplicate LOD load requests
- Add integration with world and asset streaming
- Add LOD residency diagnostics

## LOD Budgets & Quality Scaling

- Add a global triangle budget with automatic LOD bias
- Add a draw-call budget influencing LOD and merging
- Add quality presets (draft, standard, high, ultra)
- Add per-platform LOD bias and caps
- Add dynamic LOD bias driven by current framerate
- Add per-category budgets (characters, props, terrain, foliage)
- Add priority protection so key objects resist budget cuts
- Add smooth budget-driven bias changes to avoid popping
- Add over-budget diagnostics with responsible objects
- Add a user-facing quality slider mapped to LOD scaling
- Add budget reporting in stats

## Automatic Setup & User-Friendly UX

- Generate LODs automatically on import with good defaults
- Add a single toggle to enable automatic LOD
- Add automatic screen-size threshold selection
- Add plain-language presets (fewer details, balanced, high detail)
- Add sensible defaults that just work without tuning
- Add a one-click "optimize LODs" analysis and fix
- Add automatic detection of objects that need aggressive LOD
- Add guidance and warnings for missing or poor LODs
- Add a beginner mode that hides reduction internals
- Add non-technical status readouts (current detail, savings)
- Add automatic terrain LOD with no manual setup
- Add consistent, reversible LOD settings across all content

## Authoring Tools

- Add a LOD editor with per-level preview
- Add manual import of authored LOD meshes
- Add per-level reduction-setting controls
- Add per-level material assignment
- Add threshold tuning with live preview
- Add side-by-side comparison of LOD levels
- Add a wireframe and triangle-count view per level
- Add copy and reuse of LOD settings across assets
- Add mixing of authored and generated LOD levels
- Add a preview of transitions at chosen distances
- Add batch editing of LOD settings across a selection

## Editor Visualization & Debugging

- Add a LOD-level color heatmap overlay
- Add a current-LOD-per-object overlay
- Add LOD distance rings around the camera
- Add a forced-LOD view mode
- Add a triangle and draw-call HUD
- Add a transition-in-progress visualization
- Add terrain LOD tile-level visualization
- Add a per-object LOD inspector
- Add highlighting of objects missing LODs
- Add an overdraw view for transition tuning
- Add a screenshot-friendly clean LOD overlay

## Performance & Diagnostics

- Add LOD statistics (levels active, triangles, draw calls)
- Add per-object and per-category triangle counts
- Add LOD-switch frequency and cost profiling
- Add transition overdraw profiling
- Add memory reporting per LOD level
- Add warnings when LOD fails to hit budgets
- Add a headless LOD benchmark harness
- Add machine-readable LOD metrics for CI
- Add a live LOD HUD for profiling
- Add attribution of frame cost to LOD categories

## Testing & Validation

- Add mesh-simplification error and quality tests
- Add attribute-preservation tests (UVs, normals, weights)
- Add LOD-selection determinism tests
- Add transition correctness and pop-free tests
- Add terrain LOD seam-continuity tests
- Add terrain geomorph correctness tests
- Add impostor generation and rendering tests
- Add clustered-geometry LOD tests
- Add budget and quality-scaling tests
- Add golden-image tests across LOD levels
- Add large-scene LOD performance stress tests

# Capability 10 · Culling / Visibility / Occlusion

**Objective:** Deliver a unified CPU- and GPU-driven visibility pipeline that scales across views, shadows, instances, foliage, large worlds, gameplay queries, and supported rendering backends with measurable correctness and performance.

## Visibility System Core & Data Model

- Add a visibility state per renderable object
- Add per-view visibility results with visible and culled sets
- Add visibility flags (never cull, always cull, force visible)
- Add cached bounds (sphere and box) per object
- Add visibility groups for shared culling policy
- Add per-object relevance flags per pass (base, shadow, depth, custom)
- Add dirty tracking so only moved objects are re-evaluated
- Add stable object handles for cross-frame visibility coherence
- Add a visibility result buffer consumable by the renderer
- Add per-object skip reasons for debugging
- Add a unified visibility interface across meshes, terrain, and instances

## Bounds & Spatial Acceleration

- Add a broad-phase spatial acceleration structure for the scene
- Add a bounding-volume hierarchy for static content
- Add surface-area-heuristic BVH construction
- Add a fast linear BVH build from Morton codes for dynamic content
- Add binned BVH construction for build-quality control
- Add a two-level structure separating static and dynamic content
- Add a loose octree option for clustered dynamic content
- Add a uniform grid and spatial hash option for even distributions
- Add incremental insertion and removal
- Add refit of parent bounds when leaves move
- Add periodic rebuild scheduling when quality degrades
- Add parallel construction across the worker pool
- Add automatic bounds computation from geometry
- Add tight bounds recomputation for animated and skinned meshes
- Add merged group bounds for hierarchical culling
- Add cache-friendly node layout for traversal
- Add a compact quantized node format
- Add region and range queries
- Add ray queries for gameplay and picking
- Add sphere and box overlap queries
- Add nearest and k-nearest queries
- Add tree-quality metrics and rebuild heuristics
- Add memory budgets and diagnostics for the structure
- Add debug visualization of the structure and its bounds

## Frustum Culling

- Add view-frustum plane extraction from the view-projection matrix
- Add sphere-versus-frustum tests
- Add box-versus-frustum tests
- Add hierarchical frustum culling over the spatial structure
- Add early-accept for fully-inside nodes to skip subtrees
- Add SIMD-vectorized batch frustum tests
- Add per-view frustums for every active view
- Add near-plane and far-plane distance culling
- Add split frustums for shadow cascades
- Add oblique and mirrored frustums for reflections
- Add conservative tests to avoid false culling at edges
- Add frustum-culling debug visualization

## Distance & Contribution Culling

- Add a global maximum draw distance
- Add per-object cull distance
- Add cull-distance volumes that set distances by region
- Add size-on-screen thresholds for small-object culling
- Add minimum screen-coverage culling
- Add per-category distance policies (props, foliage, effects)
- Add distance-based fade-out before culling to avoid popping
- Add importance and priority overrides for key objects
- Add resolution-aware screen-size computation
- Add automatic distance tuning from performance headroom
- Add contribution-culling debug visualization

## GPU Occlusion Culling

- Build a hierarchical depth pyramid from the scene depth
- Downsample with conservative farthest-depth reduction
- Handle non-power-of-two depth with correct mip coverage
- Project object bounds to a screen-space min/max rectangle
- Select the mip level that covers the bounds rectangle in a few texels
- Compare object nearest depth against the sampled far depth
- Add a two-pass pipeline that draws last-frame visibles, rebuilds the pyramid, then tests the rest
- Draw newly-appeared objects in the second pass
- Seed occlusion from previous-frame depth reprojected by camera motion
- Handle the first frame and camera cuts without over-culling
- Add conservative bounds expansion to avoid edge popping
- Maintain a false-negative list of disoccluded objects for re-test
- Re-draw recovered objects the next frame without a visible gap
- Add per-cluster occlusion tests for clustered geometry
- Add per-meshlet occlusion tests
- Build a separate hierarchical depth per shadow view
- Add occlusion culling for reflection and planar views
- Add occlusion culling within cascaded shadow maps
- Support masked and alpha-tested occluders in the depth pyramid
- Exclude transparent objects from occluder contribution
- Add subpixel-safe tests for thin and small objects
- Tighten occludee bounds toward the geometry silhouette where affordable
- Output a GPU visibility bitfield consumable by draw generation
- Add stream compaction of survivors after occlusion
- Add occlusion-result readback for statistics with a frame delay
- Add a toggle between reprojection and two-pass modes
- Add temporal hysteresis so flickering occludees do not thrash
- Never occlude objects larger than the screen
- Add always-visible tagging that bypasses occlusion
- Add a conservative depth bias trading accuracy for safety
- Add hierarchical-depth and per-object occlusion debug views
- Add validation that occlusion never hides a truly visible object

## Hardware Occlusion Queries

- Add hardware occlusion queries for coarse per-object tests
- Add bounding-proxy rendering for query submission
- Add asynchronous query result retrieval to hide latency
- Add round-robin scheduling that amortizes queries across frames
- Add predicated rendering where the backend supports it
- Add last-known-result reuse while a query is pending
- Add conservative assume-visible on missing results
- Add batching of queries for grouped objects
- Add a capability check and fallback when queries are unavailable
- Add query-cost diagnostics and limits

## Software Occlusion Culling

- Add a CPU depth rasterizer for occluders
- Add hierarchical coverage tiles for fast rejection
- Add automatic and manual occluder selection
- Add conservative depth to avoid over-culling
- Add multi-threaded occluder rasterization
- Add per-object tests against the software depth
- Add a budget on occluders rasterized per frame
- Add merging with GPU occlusion results
- Add a fallback path where GPU occlusion is unavailable
- Add software-occlusion debug visualization

## Precomputed Visibility

- Add a cell subdivision of the scene for precomputed visibility
- Add sampling points per cell for visibility rays
- Add ray-cast sampling to determine cell-to-object visibility
- Add portal-based exact visibility computation for interiors
- Add a conservative visibility mode that never hides visible objects
- Add an aggressive mode with a tunable error tolerance
- Add an offline visibility bake step
- Add distributed and incremental baking of changed regions
- Add a memory-compact visibility-set representation
- Add compression of potentially-visible sets
- Add runtime lookup of the visible set from the camera cell
- Add streaming of visibility data with the world
- Add merging of precomputed visibility with runtime culling
- Add validation that precomputed sets never hide visible objects
- Add precomputed-visibility debug visualization

## Portals & Cells

- Add a portal-and-cell graph for interiors
- Add cell membership assignment for objects
- Add portal traversal culling from the camera cell
- Add portal frustum narrowing through each opening
- Add recursive traversal with visited-cell tracking
- Add antiportals for large blocking occluders
- Add door and window portals that open and close
- Add interior-to-exterior portal handoff
- Add mirror and view portals for reflections and see-through
- Add automatic cell and portal generation helpers
- Add portal-graph authoring and editing tools
- Add portal and cell debug visualization

## Occluder Authoring

- Add occluder meshes and volumes
- Add automatic simplified-occluder generation from geometry
- Add occluder and occludee flags per object
- Add box, plane, and volume occluder primitives
- Add occluder quality and budget controls
- Add terrain as an automatic occluder
- Add large static meshes as automatic occluders
- Add occluder validation for watertightness and size
- Add occluder authoring and preview tools
- Add occluder debug visualization

## GPU-Driven Culling

- Add a GPU scene description of instances, bounds, and materials
- Add persistent GPU buffers updated incrementally each frame
- Add compute-shader frustum culling of all instances
- Add compute-shader distance and contribution culling
- Add compute-shader occlusion culling of all instances
- Add per-cluster frustum culling
- Add per-cluster cone (backface) culling
- Add per-cluster occlusion culling
- Add per-meshlet culling for a mesh-shader path
- Add a task/amplification stage that expands visible work
- Add stream compaction of survivors into dense draw lists
- Add indirect draw-argument generation on the GPU
- Add multi-draw-indirect submission from generated arguments
- Add per-view GPU culling for main, shadow, and reflection passes
- Add sorting and batching of survivors by pipeline and material
- Add two-level culling that rejects whole clusters before instances
- Add a visibility bitfield shared across passes within a frame
- Add persistent-thread and wave-efficient culling kernels
- Add a mesh-shader path and a fallback vertex path
- Add a full CPU fallback where compute or indirect is unavailable
- Add GPU-culling counters and readback for diagnostics
- Add deterministic GPU culling for tests and replay
- Add per-instance LOD selection fused into the culling pass
- Add shadow-caster expansion handled on the GPU
- Add GPU-culling debug visualization

## Per-View & Multi-View Culling

- Add independent culling per active view
- Add shared broad-phase results reused across views
- Add culling for shadow views and cascades
- Add culling for reflection and planar views
- Add culling for multiple cameras and split-screen
- Add view-relevance flags to skip irrelevant passes
- Add merged culling for views sharing a frustum region
- Add per-view budgets and priorities
- Add cross-view visibility caching where valid
- Add a combined-frustum pre-pass across all shadow cascades
- Add per-view culling statistics

## Shadow-Caster Culling

- Add culling of shadow casters per light
- Add culling of casters per shadow cascade
- Add caster culling by shadow-frustum bounds
- Add culling of casters that cannot affect visible receivers
- Add extrusion of caster bounds along the light direction
- Add distance and size culling for shadow casters
- Add occlusion culling within shadow views
- Add merged caster culling across cascades
- Add a receiver-region test to bound relevant casters
- Add shadow-caster culling statistics
- Add shadow-caster culling debug visualization

## Backface & Cluster Cone Culling

- Add backface culling configuration per material
- Add two-sided and double-sided handling
- Add meshlet and cluster cone culling
- Add per-cluster normal-cone data generation
- Add degenerate and zero-area triangle rejection
- Add orientation-aware culling for instanced content
- Add cone-culling integration with GPU-driven culling
- Add cone-culling correctness validation
- Add cone-culling debug visualization

## Ray-Traced & Distance-Field Visibility

- Add signed-distance-field proxies for coarse occlusion
- Add cone or ray tracing against distance fields for visibility
- Add a global distance field assembled from object fields
- Add ray-traced occlusion where hardware ray tracing is available
- Add distance-field occlusion as a fallback without ray tracing
- Add distance-field-based long-range visibility for streaming
- Add caching and temporal reuse of ray-traced visibility
- Add quality and cost controls for ray-traced visibility
- Add validation against rasterized occlusion results
- Add distance-field and ray-visibility debug visualization

## Temporal & Predictive Visibility

- Add temporal coherence reuse of last-frame visibility
- Add camera-cut detection that invalidates temporal data
- Add predictive visibility from camera velocity
- Add latency hiding by prefetching soon-visible content
- Add disocclusion detection and recovery
- Add round-robin re-testing to amortize occlusion cost
- Add temporal smoothing of fade-in and fade-out
- Add a bounded staleness so stale visibility is refreshed
- Add validation that temporal reuse never hides visible objects
- Add temporal-visibility statistics
- Add temporal-visibility debug visualization

## Foliage & Instance Culling

- Add per-instance frustum culling
- Add cluster and cell culling before per-instance tests
- Add per-instance occlusion culling
- Add density and contribution culling for instances
- Add SIMD-vectorized instance culling over chunks
- Add GPU-driven instance culling
- Add per-view instance culling for shadows and reflections
- Add shared culling policy with the foliage system
- Add instance-culling statistics
- Add instance-culling debug visualization

## Visibility Queries & Gameplay

- Add an is-visible query for a given object and view
- Add line-of-sight queries between points
- Add visibility-enter and visibility-leave events
- Add on-screen and off-screen notifications for gameplay
- Add relevance queries for AI perception and streaming
- Add coarse gameplay occlusion checks decoupled from rendering
- Add batched visibility queries for many agents
- Add distance and angle visibility helpers
- Add scripting API for visibility queries and events
- Add deterministic query results for replay and tests

## ECS Integration & Bulk Culling

- Run culling over archetype chunks in a data-oriented layout
- Add SIMD frustum-culling kernels over chunks
- Add SIMD distance and contribution kernels over chunks
- Add parallel culling across the worker pool
- Add job-graph scheduling of broad-phase, frustum, and occlusion stages
- Add bulk visibility-result writes to packed buffers
- Add cache-friendly bounds layouts for culling
- Add zero-copy handoff of survivors to GPU draw lists
- Add memory-traffic-aware batch sizes for culling
- Add deterministic parallel culling stable across thread counts
- Add scaling to millions of cullable objects within budget
- Add incremental culling that reuses results for static objects

## Budgets, Quality & Scaling

- Add a per-frame culling time budget
- Add quality scaling of occlusion accuracy
- Add adaptive occluder and instance limits from headroom
- Add per-platform culling feature selection
- Add fallbacks when advanced culling is unavailable
- Add graceful degradation under heavy load
- Add priority so key objects are never wrongly culled
- Add budget over-run diagnostics
- Add a quality-preset mapping for culling
- Add culling-cost reporting in stats

## Editor Tools & Visualization

- Add a frozen-frustum mode to inspect culling from a fixed view
- Add a culling-statistics overlay
- Add per-object culling-reason display
- Add hierarchical-depth and occlusion visualization
- Add spatial-structure and bounds visualization
- Add occluder and portal visualization
- Add a visible-set highlight and culled-set dimming
- Add distance-ring and cull-distance visualization
- Add a false-culling detector that flags disappearing objects
- Add a per-view culling inspector
- Add a screenshot-friendly clean culling overlay
- Add a step-through of culling stages for a captured frame

## Performance & Diagnostics

- Add culling statistics (tested, culled, visible per stage)
- Add per-stage culling timing
- Add false-positive and false-negative tracking
- Add per-view and per-pass culling breakdowns
- Add occlusion-query and readback latency reporting
- Add a headless culling benchmark harness
- Add machine-readable culling metrics for CI
- Add a live culling HUD for profiling
- Add attribution of frame cost to culling stages
- Add warnings when culling exceeds its budget

## Testing & Validation

- Add frustum-culling correctness tests
- Add occlusion-culling no-false-culling tests
- Add culling determinism tests for a fixed view path
- Add precomputed-visibility correctness tests
- Add portal-and-cell traversal tests
- Add shadow-caster culling correctness tests
- Add per-instance culling correctness tests
- Add temporal-reuse safety tests
- Add GPU-versus-CPU culling parity tests
- Add large-scene culling performance stress tests
- Add golden-image tests comparing culled and reference renders

# Capability 11 · Level / Scene Management

**Objective:** Deliver versioned scene composition, asynchronous lifecycle management, nested content, play-mode transitions, validation, persistence, source-control-friendly authoring, and reliable loading for both small and streamed worlds.

## Scene Data Model & Format

- Add a scene asset holding a root hierarchy and entity list
- Add serialized component data per entity
- Add stable per-entity identifiers unique within a scene
- Add global identifiers for cross-scene references
- Add a scene dependency manifest (assets, other scenes, prefabs)
- Add per-scene settings (lighting, environment, physics, post-process)
- Add a distinction between embedded and referenced content
- Add scene bounds computed from content
- Add scene metadata (name, description, tags, author, timestamps)
- Add a binary scene format for fast loading
- Add a diff-friendly text scene format for source control
- Add conversion between binary and text formats
- Add a versioned scene format with migration
- Add integrity checksums and validation on load
- Add forward-compatible handling of unknown fields
- Add per-scene content manifests generated on save
- Add references from a scene to the assets it uses
- Add a stable reference type surviving rename and move

## Scene Loading Pipeline

- Add synchronous scene loading
- Add fully asynchronous loading off the main thread
- Add progress reporting with weighted stages
- Add dependency resolution and preloading before instantiation
- Add staged instantiation spread across frames to avoid hitches
- Add loading into a fresh world
- Add loading additively into the current world
- Add background deserialization of scene content
- Add staged GPU upload of scene assets
- Add job-system integration for parallel load work
- Add load priorities and queue ordering
- Add cancellation of in-flight loads
- Add load coalescing for duplicate requests
- Add retry and error handling for failed loads
- Add load-completion callbacks and events
- Add a placeholder or loading state while content instantiates
- Add hot-reload of a scene edited on disk
- Add deterministic load ordering for reproducibility
- Add a preload API that warms assets without instantiating
- Add memory-budget-aware loading

## Scene Unloading

- Add unloading of a loaded scene
- Add safe destruction of a scene's entities
- Add release of assets no longer referenced by any scene
- Add reference counting so shared assets survive
- Add deferred unload to the end of the frame
- Add unload events and completion callbacks
- Add partial unload of a region or layer within a scene
- Add cleanup validation that no dangling references remain
- Add async unload without frame stalls

## Multi-Scene & Composition

- Add additive loading of multiple scenes at once
- Add an active-scene concept for where new content is created
- Add moving entities between loaded scenes
- Add cross-scene references resolved as scenes load and unload
- Add a master/composition scene that references child scenes
- Add per-scene enable and disable
- Add scene ownership of the entities it created
- Add merging of several scenes into one
- Add splitting a scene into multiple scenes
- Add ordering and priority among loaded scenes
- Add conflict handling for identifiers across additive scenes
- Add a query for which scene an entity belongs to
- Add persistence of the active loaded-scene set
- Add lazy resolution of references to not-yet-loaded scenes

## Scene Lifecycle & Transitions

- Add a transition manager coordinating unload and load
- Add optional loading screens during transitions
- Add fade-out and fade-in transitions
- Add async pre-load of the destination before activation
- Add seamless handoff with no loading screen where possible
- Add persistent objects that survive a scene transition
- Add a minimum-display-time for loading screens
- Add cancelable and interruptible transitions
- Add transition events (started, progress, finished)
- Add ordered sequencing (fade, unload old, load new, fade back)
- Add a startup and splash flow before the first scene
- Add carry-over of state across transitions
- Add a fallback loading screen when streaming cannot keep up
- Add validation that transitions release the previous scene

## Sub-Scenes & Nested Scenes

- Add nested scene references embedded in a parent scene
- Add independent streaming of sub-scenes
- Add per-instance overrides on nested scenes
- Add lifecycle propagation to nested scenes
- Add cycle detection for nested scene references
- Add editing a nested scene in isolation
- Add depth limits and diagnostics for deep nesting
- Add stable references into nested scene content

## Editor Scene Management

- Add opening multiple scenes in the editor at once
- Add a scene tab or list for switching between open scenes
- Add selecting the active scene for new content
- Add a per-scene hierarchy panel
- Add drag-and-drop of entities between open scenes
- Add per-scene dirty tracking
- Add save, save-as, and save-all
- Add unsaved-changes prompts on close
- Add isolate or solo of a single scene in the viewport
- Add show and hide of individual open scenes
- Add a recent-scenes list
- Add close and reorder of open scenes
- Add creating a new empty scene
- Add duplicating an existing scene
- Add renaming a scene with reference fixup
- Add per-scene lock to prevent accidental edits
- Add indication of which scene owns the current selection
- Add reload-from-disk of a scene discarding edits

## Play Mode & Simulation

- Add entering and exiting play mode from the editor
- Add snapshotting the world before play begins
- Add restoring the exact pre-play state on exit
- Add play starting from the current scene
- Add play starting from the configured bootstrap scene
- Add pause and single-frame step in play mode
- Add isolation so play-mode changes never touch saved scenes
- Add a fast enter-play path that avoids a full reload
- Add a policy for edits made during play
- Add a deterministic world reset between play sessions
- Add simulate mode without possessing a player
- Add capture of play-mode state into a new scene

## Bootstrapping & Game Flow

- Add a configurable startup scene
- Add a boot sequence that runs before gameplay
- Add a persistent bootstrap scene that stays loaded
- Add a scene stack and history for navigation
- Add return-to-previous and return-to-menu flows
- Add a main-menu to gameplay flow
- Add a data-driven game-flow description
- Add flow events consumable by gameplay and scripts
- Add per-build overrides of the startup scene
- Add safe recovery when the startup scene is missing

## Scene References & Dependencies

- Add referencing scenes and content by stable identifier
- Add a dependency graph across scenes and assets
- Add missing-reference detection and reporting
- Add reference fixup when a target is renamed or moved
- Add a fallback for unresolved references
- Add a dependency browser in the editor
- Add detection of circular scene dependencies
- Add validation that all references resolve before save
- Add preloading of referenced scenes and assets
- Add reference-usage search across the project

## Scene Bounds, Metadata & Thumbnails

- Add automatic scene-bounds computation
- Add editable scene metadata fields
- Add scene thumbnail capture from a view
- Add scene statistics (entity count, memory, asset count)
- Add tags and categories for organizing scenes
- Add a scene browser showing thumbnails and metadata
- Add last-edited and authorship tracking

## Level Bake & Preprocessing

- Add association of baked lighting with a scene
- Add baked navigation data per scene
- Add baked occlusion and visibility data per scene
- Add baked reflection and light probes per scene
- Add a cook step that prepares a scene for shipping
- Add incremental rebake of only changed regions
- Add bake status and dirty tracking
- Add validation that baked data matches current content
- Add background baking that does not block editing
- Add bake artifacts stored alongside the scene

## Scene Validation

- Add pre-save scene validation
- Add detection of missing references
- Add detection of duplicate identifiers
- Add detection of orphaned and unreachable entities
- Add detection of content outside scene bounds
- Add checks for required components and settings
- Add one-click fixups for common problems
- Add a validation report panel
- Add customizable validation rules
- Add validation as a step in the build pipeline

## Scene Diff, Merge & Version Control

- Add a granular per-object file layout for scenes
- Add a text format that produces readable diffs
- Add diffing of two scene versions
- Add three-way merge of scene changes
- Add conflict detection and resolution
- Add visualization of scene changes in the editor
- Add version-control status per scene and per object
- Add locking of scenes or objects during team edits
- Add merge-friendly stable ordering of serialized content

## Persistence & Runtime State

- Add persistence of runtime changes to a scene
- Add save and restore of the active loaded-scene set
- Add scene state captured in save games
- Add per-scene reset to authored state
- Add a distinction between authored content and runtime changes
- Add versioned migration of persisted scene state

## Editor UX

- Add a guided new-scene wizard with templates
- Add scene templates and create-from-template
- Add a default scene for new projects
- Add autosave and crash-safe recovery of open scenes
- Add clear status of loaded, active, and dirty scenes
- Add plain-language prompts for save and discard
- Add drag-and-drop of scenes into the world
- Add a beginner-friendly single-scene mode
- Add a gallery of example scenes to open and learn from

## Performance & Large Scenes

- Add async loading and unloading that never stalls the editor
- Add load-time budgets and profiling per scene
- Add memory reporting per loaded scene
- Add handoff to world streaming for very large scenes
- Add progressive instantiation to smooth load spikes
- Add background prewarming of likely-next scenes
- Add diagnostics for slow-loading scenes

## Testing & Validation

- Add load and unload lifecycle tests
- Add additive multi-scene tests
- Add cross-scene reference resolution tests
- Add transition sequencing tests
- Add reference-fixup tests for rename and move
- Add binary and text round-trip serialization tests
- Add play-mode enter and restore fidelity tests
- Add scene-migration tests across versions
- Add save and restore fidelity tests for scene state
- Add large-scene load-performance stress tests
- Add validation-rule regression tests

# Capability 12 · Procedural Generation / PCG

**Objective:** Deliver a deterministic, extensible procedural-generation graph and runtime capable of producing reusable world, terrain, vegetation, geometry, structure, material, and gameplay content with non-destructive authoring and bounded execution.

## PCG Graph Framework

- Add a node-based procedural generation graph
- Add typed pins for points, attributes, meshes, splines, and volumes
- Add a node registry with categories and metadata
- Add graph compilation into an executable plan
- Add a dependency-driven evaluation order
- Add lazy evaluation that runs only what an output needs
- Add caching of intermediate node results
- Add incremental re-evaluation when a single node or parameter changes
- Add dirty propagation through downstream nodes
- Add subgraphs and collapsed reusable node groups
- Add user-defined graph functions with inputs and outputs
- Add exposed parameters promoted to the graph interface
- Add loop and iteration constructs with guards
- Add branch and switch nodes for conditional generation
- Add multiple named outputs per graph
- Add graph-level seed and deterministic evaluation
- Add per-node timing and cost accounting
- Add graph versioning and migration
- Add error and warning propagation with node attribution
- Add cancellation of long-running graph evaluation
- Add partial evaluation limited to a spatial region
- Add a graph interpreter and an optional compiled fast path
- Add graph serialization in binary and diff-friendly text
- Add hot-reload of graphs edited on disk

## PCG Data Model

- Add a point-cloud data type with position and orientation
- Add typed attribute sets attached to points
- Add scalar, vector, color, bool, integer, and string attributes
- Add per-point transform (position, rotation, scale)
- Add density and weight as first-class attributes
- Add a spatial-data type carrying bounds and sampling
- Add surface data sampled from meshes and terrain
- Add volume data for 3D fields and voxels
- Add spline data with points, tangents, and width
- Add attribute metadata (range, default, interpolation)
- Add attribute creation, copy, and removal nodes
- Add attribute interpolation and blending
- Add tags and classification attributes on points
- Add a stable per-point identifier for cross-graph references
- Add a compact columnar attribute layout for performance
- Add streaming-friendly chunked point storage
- Add conversion between points, meshes, splines, and volumes
- Add attribute schema validation
- Add memory accounting for point and attribute data
- Add debug inspection of any data on any pin

## Sampling & Scattering Nodes

- Add surface scattering that samples points on meshes and terrain
- Add volume scattering that fills a 3D region
- Add spline scattering along and around paths
- Add grid and jittered-grid sampling
- Add Poisson-disk sampling for even spacing
- Add blue-noise sampling for natural distribution
- Add density-driven sample counts from a map or field
- Add clustered scattering for clumps and groves
- Add stratified sampling for controlled coverage
- Add importance sampling weighted by an attribute
- Add relaxation to even out an existing point set
- Add minimum-distance enforcement between points
- Add per-sample random seed derived deterministically
- Add surface-normal and slope capture at each sample
- Add barycentric and UV capture at each sample
- Add boundary and edge sampling
- Add sampling limited by a mask or selection
- Add sample-count budgets and caps
- Add adaptive sampling that refines where needed
- Add resampling and thinning of dense point sets
- Add scatter debug visualization
- Add deterministic scattering stable across runs

## Filtering & Selection Nodes

- Add filtering by attribute threshold and range
- Add filtering by slope, height, and curvature
- Add filtering by proximity to other points or objects
- Add filtering by a mask or painted region
- Add filtering by inside/outside a volume or spline
- Add random selection by percentage and count
- Add selection by density comparison
- Add selection by tag and classification
- Add top-N and bottom-N selection by an attribute
- Add set operations (union, intersection, difference) on point sets
- Add spatial partitioning of a set into groups
- Add expression-based selection with a small formula language
- Add invert, expand, and contract of a selection
- Add stable ordering of filtered results
- Add selection debug visualization

## Transform & Geometry Nodes

- Add translate, rotate, and scale nodes on point transforms
- Add align-to-normal and align-to-direction nodes
- Add random rotation, scale, and jitter within ranges
- Add snap-to-surface and project-onto-surface nodes
- Add snap-to-grid and snap-to-spline nodes
- Add offset along normal and along axis nodes
- Add look-at and orient-between-points nodes
- Add curve-following orientation along splines
- Add bounds and pivot manipulation
- Add copy-to-points that instances geometry at each point
- Add mirror, array, and radial-array nodes
- Add relax and smooth of point positions
- Add noise-based displacement of transforms
- Add attribute-driven transform (scale by density, rotate by slope)
- Add merge, append, and split of point sets
- Add reorder and sort by an attribute
- Add transform debug visualization

## Attribute & Math Nodes

- Add arithmetic nodes over attributes
- Add vector and matrix math nodes
- Add remap, clamp, and curve nodes
- Add random-value nodes seeded deterministically
- Add gradient and ramp evaluation nodes
- Add comparison and logic nodes
- Add attribute copy, rename, and delete nodes
- Add attribute-from-position and from-normal nodes
- Add attribute blending and interpolation nodes
- Add accumulate and reduce nodes over a set
- Add per-neighbor aggregation nodes
- Add a compact expression node with a formula language
- Add color and gradient sampling nodes
- Add noise-to-attribute nodes
- Add conditional attribute assignment
- Add attribute normalization and statistics

## Spatial Query & Proximity Nodes

- Add nearest-neighbor queries between point sets
- Add radius and k-nearest neighbor queries
- Add distance-to-surface and distance-to-spline nodes
- Add ray and line queries against surfaces
- Add overlap and containment tests against volumes
- Add density estimation from local neighborhoods
- Add clustering by proximity
- Add graph and connectivity building between points
- Add shortest-path and network queries over point graphs
- Add spatial acceleration reuse across queries
- Add occupancy and collision checks for placement
- Add proximity debug visualization

## Noise & Field Nodes

- Add value noise
- Add gradient (Perlin) noise
- Add simplex noise
- Add cellular (Worley) noise with multiple distance metrics
- Add fractal Brownian motion layering
- Add ridged and billowed noise variants
- Add domain warping of noise inputs
- Add Voronoi cells and edges
- Add Delaunay triangulation of point sets
- Add curl and flow noise for directional fields
- Add gradient and vector fields
- Add distance fields from points, splines, and surfaces
- Add tiling and seamless noise options
- Add analytic derivatives for slope and normals
- Add noise combination (add, multiply, blend, warp)
- Add frequency, octave, lacunarity, and gain controls
- Add masks and falloff shapes as fields
- Add a field-to-attribute sampling node
- Add GPU evaluation of noise and fields
- Add deterministic, seed-driven noise
- Add field debug visualization

## Spawning & Output Nodes

- Add mesh spawning at points
- Add instanced-mesh output for dense placement
- Add entity spawning into the world from points
- Add prefab and chunk instancing from points
- Add material and variation assignment on spawn
- Add per-instance attribute passthrough (color, wind, scale)
- Add spline and mesh generation as output
- Add terrain height and weight output
- Add foliage-layer output handed to the foliage system
- Add collision and physics setup on spawned content
- Add navigation flags on spawned content
- Add LOD assignment on spawned content
- Add tagging and grouping of spawned content
- Add cleanup and regeneration that removes prior output
- Add bulk spawn through the deferred command buffer
- Add streaming-aware output tied to cells
- Add output budgets and caps
- Add output debug visualization

## Density, Masks & Weight Maps

- Add painted density and weight masks as inputs
- Add mask generation from slope, height, and curvature
- Add mask generation from distance fields
- Add mask combination (add, multiply, min, max, subtract)
- Add mask blur, sharpen, and threshold
- Add mask remap and curve shaping
- Add mask from image and imported data
- Add mask from selection and tags
- Add mask preview and visualization
- Add per-layer mask stacks
- Add mask-driven density in sampling nodes
- Add mask baking to a reusable asset
- Add mask resolution and memory controls

## Rule & Constraint Solving

- Add a tile set with adjacency rules
- Add constraint-based tile solving over a grid
- Add wave-function-collapse-style propagation
- Add backtracking and contradiction recovery
- Add weighted tile probabilities
- Add edge and corner socket matching
- Add 3D tile solving for volumes
- Add pre-placed constraints and seeds
- Add border and boundary constraints
- Add symmetry and rotation of tiles
- Add hierarchical constraint solving for large grids
- Add region-limited and incremental solving
- Add deterministic solving from a seed
- Add solver timeout and fallback handling
- Add authoring of tile sets and adjacency
- Add solver debug visualization of collapse steps
- Add validation that solutions satisfy all constraints

## Grammar & L-System Generation

- Add a rule-based grammar system
- Add an L-system interpreter for branching structures
- Add stochastic and context-sensitive rules
- Add parameterized grammar symbols
- Add turtle-style geometry interpretation
- Add shape-grammar subdivision of volumes
- Add facade and floor-plan grammars
- Add plant and tree grammars
- Add rule weighting and randomization
- Add recursion depth limits and guards
- Add grammar authoring and editing tools
- Add deterministic grammar expansion from a seed
- Add grammar-to-mesh and grammar-to-points output
- Add grammar debug visualization of derivation

## Voxel & Volumetric Generation

- Add a voxel density field data type
- Add signed-distance-field authoring and combination
- Add boolean operations on volumes (union, subtract, intersect)
- Add smooth and sharp blending of volumes
- Add marching-cubes surface extraction
- Add dual-contouring for sharp features
- Add adaptive resolution for volume meshing
- Add material assignment per voxel region
- Add noise and field-driven volume carving
- Add cave and tunnel carving operators
- Add overhang and arch generation
- Add volume-to-collision generation
- Add chunked voxel volumes for streaming
- Add GPU-assisted volume meshing
- Add seam-free meshing across chunk boundaries
- Add volume debug visualization

## Procedural Mesh Generation

- Add a procedural mesh builder with vertices and indices
- Add primitive generators (plane, box, sphere, cylinder, cone)
- Add extrude, bevel, and inset operations
- Add loft and sweep along splines
- Add revolve and lathe operations
- Add boolean mesh operations
- Add subdivision and smoothing
- Add automatic UV generation and unwrapping
- Add automatic normal and tangent generation
- Add vertex-color and attribute painting on generated meshes
- Add mesh triangulation of point sets and outlines
- Add mesh simplification of generated geometry
- Add automatic LOD generation for procedural meshes
- Add collision generation for procedural meshes
- Add mesh caching and reuse
- Add procedural-mesh output to the spawn system
- Add mesh validation (manifold, winding, degenerate checks)

## Terrain Generation

- Add heightfield generation from noise and fields
- Add layered terrain generation with combined octaves
- Add ridge, mountain, and valley operators
- Add hydraulic and thermal erosion nodes
- Add river and lake carving from flow
- Add slope, height, and curvature masks for materials
- Add automatic material-weight generation
- Add automatic scatter placement on generated terrain
- Add terrain-hole and cave-entrance generation
- Add region-limited terrain generation inside a brush
- Add blending of generated terrain with hand edits
- Add seamless generation across terrain tiles
- Add real-world data as a generation input
- Add deterministic terrain generation from a seed
- Add output directly into the terrain system
- Add terrain-generation presets (islands, mountains, deserts)
- Add live preview of terrain generation
- Add non-destructive terrain-generation layers

## Scatter & Ecosystem Generation

- Add ecosystem rules combining species, density, and constraints
- Add competition and exclusion between species
- Add layered vegetation (canopy, understory, ground cover)
- Add moisture, temperature, and light inputs
- Add slope, height, and soil constraints
- Add clustering and grouping into natural stands
- Add age and size variation across placements
- Add avoidance around roads, water, and structures
- Add succession and growth simulation over time
- Add seed-dispersal-style spreading
- Add density and weight maps for control
- Add deterministic ecosystem generation from a seed
- Add output directly into the foliage system
- Add ecosystem presets (forest, meadow, tundra, jungle)
- Add live repopulation when the surface changes
- Add ecosystem debug visualization
- Add region-limited ecosystem painting

## Road, Path & Network Generation

- Add road-network generation from a graph
- Add cost-based path routing over terrain
- Add slope and obstacle avoidance in routing
- Add intersection and junction generation
- Add hierarchical networks (highways, streets, alleys)
- Add path smoothing and banking
- Add bridge and tunnel generation where paths cross obstacles
- Add terrain conforming and carving along paths
- Add roadside prop and detail placement
- Add river-network generation from flow accumulation
- Add trail and footpath generation between points of interest
- Add spline output for downstream tools
- Add deterministic network generation from a seed
- Add connectivity validation of the network
- Add network debug visualization
- Add network presets (grid city, organic town, rural)

## Building & Structure Generation

- Add modular building assembly from a kit of parts
- Add floor-plan generation with rooms and connections
- Add facade generation with windows, doors, and trim
- Add multi-floor stacking with consistent alignment
- Add roof generation (flat, gabled, hipped, complex)
- Add stair and connection generation between floors
- Add interior wall and partition generation
- Add furniture and prop placement inside rooms
- Add structural constraint checks (support, openings)
- Add material and style themes for buildings
- Add level-of-detail proxies for distant buildings
- Add damage, age, and wear variation
- Add collision and navigation for generated buildings
- Add snapping of buildings to terrain and plots
- Add deterministic building generation from a seed
- Add building presets and style libraries
- Add building-generation debug visualization
- Add non-destructive editing of generated buildings

## City & Urban Layout Generation

- Add block and parcel subdivision from a road network
- Add lot allocation and building placement per parcel
- Add zoning (residential, commercial, industrial, parks)
- Add population-density-driven building height and density
- Add landmark and point-of-interest placement
- Add street furniture, lighting, and signage placement
- Add sidewalk, plaza, and open-space generation
- Add district and neighborhood theming
- Add terrain-aware city layout on uneven ground
- Add river and coastline integration into the layout
- Add traffic and pedestrian route hints
- Add deterministic city generation from a seed
- Add city presets (grid, medieval, modern, coastal)
- Add city-generation debug visualization
- Add region-limited and incremental city generation

## Interior & Dungeon Generation

- Add room-graph generation with connections
- Add grid, cellular, and organic layout algorithms
- Add corridor and hallway carving between rooms
- Add door, lock, and key placement
- Add room templates and hand-authored set-pieces
- Add encounter, loot, and spawn placement
- Add difficulty and pacing curves across the layout
- Add guaranteed connectivity and reachability
- Add critical-path and optional-branch generation
- Add theming and biome variation per region
- Add trap and hazard placement
- Add prop and decoration scattering
- Add multi-floor and vertical dungeon generation
- Add mesh assembly from the layout
- Add collision and navigation for generated interiors
- Add deterministic dungeon generation from a seed
- Add validation that every area is reachable
- Add dungeon-generation debug visualization

## Cave & Cavern Generation

- Add cave generation from volumetric fields
- Add tunnel carving between chambers
- Add chamber and cavern shaping operators
- Add stalactite, stalagmite, and formation placement
- Add water and pool placement in caves
- Add entrance and exit generation connecting to the surface
- Add mesh extraction and material assignment
- Add collision and navigation for caves
- Add chunked cave generation for streaming
- Add deterministic cave generation from a seed
- Add cave-generation debug visualization

## Biome & World Generation

- Add a world-scale biome map from climate inputs
- Add temperature, moisture, and elevation models
- Add biome assignment and blending zones
- Add continent, ocean, and coastline generation
- Add mountain-range and plate-style features
- Add river-network generation across the world
- Add biome-specific terrain, scatter, and material rules
- Add points-of-interest and settlement placement
- Add world-map preview and overview
- Add seed-driven whole-world generation
- Add region extraction for detailed generation
- Add layering of hand-authored regions over generated ones
- Add streaming-aware world generation
- Add world-generation presets (archipelago, continent, wasteland)
- Add world-generation debug visualization
- Add validation of biome coverage and transitions

## Procedural Materials & Texturing

- Add procedural texture generation from noise and fields
- Add layered material blending driven by generated masks
- Add weathering and aging effects from curvature and cavity
- Add per-object color and pattern variation
- Add procedural decal and detail placement
- Add tiling and macro-variation control
- Add baking of procedural textures to assets
- Add handoff to the material system
- Add deterministic texture generation from a seed
- Add live preview of procedural materials
- Add procedural-material presets
- Add resolution and memory controls

## Runtime & Endless Generation

- Add runtime evaluation of graphs during play
- Add generation triggered by streaming cell load
- Add endless and infinite-world generation around the camera
- Add chunked generation aligned to world cells
- Add seamless generation across chunk boundaries
- Add async generation off the main thread
- Add background prefetch of soon-visible generation
- Add deterministic generation so a seed reproduces the world
- Add regeneration when parameters or inputs change
- Add partial regeneration of only affected regions
- Add generation budgets to avoid frame hitches
- Add caching of generated chunks to disk
- Add eviction of distant generated chunks
- Add persistence of runtime edits over generated content
- Add reconciliation of hand edits with regenerated content
- Add gameplay-driven generation (spawn a structure on demand)
- Add streaming handoff of generated content to the world system
- Add generation progress reporting
- Add cancellation of in-flight generation
- Add runtime generation diagnostics

## Determinism & Seeding

- Add a global generation seed
- Add per-graph and per-node seed derivation
- Add spatially stable seeding so a location always generates the same
- Add counter-based reproducible random streams
- Add order-independent seeding for parallel evaluation
- Add seed exposure in parameters and presets
- Add reproducibility across platforms and hardware
- Add seed-variation tools to explore alternatives
- Add locking of seeds for finalized content
- Add determinism validation across runs
- Add determinism validation across thread counts

## ECS Integration & Bulk Generation

- Spawn generated content as archetype/chunk data
- Add bulk entity creation through the deferred command buffer
- Add parallel graph evaluation across the worker pool
- Add SIMD-friendly point and attribute layouts
- Add job-graph scheduling of generation stages
- Add chunked evaluation matching ECS chunk sizes
- Add zero-copy handoff of generated instances to GPU buffers
- Add memory-traffic-aware batch sizes for generation
- Add deterministic parallel generation stable across threads
- Add streaming of generated ECS chunks in and out
- Add bulk despawn and recycling of generated entities
- Add scaling to millions of generated instances within budget
- Add generation that reuses foliage and instancing paths
- Add cache-friendly output layouts for downstream systems
- Add throughput diagnostics for bulk generation
- Add a stress harness for extreme generated counts

## Streaming & World Integration

- Add per-cell generation tied to world streaming
- Add deterministic per-cell seeds from cell coordinates
- Add seam matching so adjacent cells align
- Add generation ahead of the streaming radius
- Add eviction of generated content with cell unload
- Add persistence of edits to generated cells
- Add origin-rebasing awareness for generated content
- Add priority generation around streaming sources
- Add proxy generation for distant regions
- Add handoff of generated collision and navigation
- Add generated-content residency budgets
- Add integration with terrain, foliage, and water systems
- Add streaming-generation diagnostics

## Non-Destructive Procedural Stack

- Add a procedural layer stack over authored content
- Add per-layer graphs applied in order
- Add masks limiting where each layer applies
- Add blend modes between procedural and authored content
- Add reorder, toggle, and solo of procedural layers
- Add preservation of hand edits under regeneration
- Add bake of the stack into final content on demand
- Add per-layer parameters and presets
- Add live recompute with cached intermediate results
- Add copy and reuse of procedural layers across content
- Add a stack-cost readout

## Graph Authoring & Editor

- Add a node-graph editor canvas
- Add a searchable node palette with categories
- Add drag-to-connect typed pins with validation
- Add live preview of graph output in the viewport
- Add per-node preview of intermediate data
- Add exposed-parameter panels with plain-language labels
- Add subgraph creation, collapse, and expand
- Add comments, groups, and reroute nodes
- Add copy, paste, and duplicate of node selections
- Add undo and redo across graph edits
- Add a node reference and inline documentation
- Add graph templates and starting points
- Add a graph library and reusable modules
- Add error and warning highlighting on nodes
- Add pin value inspection and pinning
- Add on-canvas parameter tweaking with live update
- Add layout auto-arrange and alignment helpers
- Add search and navigation within large graphs
- Add versioned graph assets with migration
- Add a gallery of example graphs to open and learn from
- Add a beginner mode with simplified nodes
- Add graph diff and merge for source control

## Debugging & Data Inspection

- Add visualization of points, densities, and attributes
- Add attribute heatmaps and color mapping
- Add per-node data statistics (count, ranges, memory)
- Add step-through evaluation of the graph
- Add isolation of a single node's output
- Add timing and cost profiling per node
- Add highlighting of the most expensive nodes
- Add inspection of seeds and random draws
- Add comparison of two graph results
- Add capture and replay of a generation run
- Add warnings for empty or degenerate outputs
- Add validation of attribute schemas across the graph
- Add a data inspector for any pin

## One-Click Generators & Templates

- Add one-click "generate a forest" over a region
- Add one-click "generate a city" on the terrain
- Add one-click "generate a dungeon"
- Add one-click "generate caves" under the terrain
- Add one-click "generate a mountain range"
- Add one-click "generate a river system"
- Add one-click "scatter rocks and debris"
- Add one-click "populate this area" from environment rules
- Add plain-language parameters (more trees, denser, wilder)
- Add sensible defaults that produce good results immediately
- Add draw-a-region-then-generate workflow
- Add brush-based procedural painting of rules
- Add live preview before committing a generator
- Add always-available undo of generated content
- Add a generator gallery with thumbnails
- Add guided wizards for complex generators
- Add a beginner mode that hides graph internals
- Add one-click regenerate with a new seed

## Presets, Parameters & Reusable Modules

- Add named presets bundling a graph and its parameters
- Add a preset library with categories and thumbnails
- Add exposed parameters with ranges and defaults
- Add parameter randomization within safe ranges
- Add reusable module graphs shared across projects
- Add capture of a generated result into a preset
- Add layering of presets and overrides
- Add import and export of presets and modules
- Add versioning of presets and modules
- Add parameter validation and dependency rules
- Add a favorites and recents list
- Add community-shareable preset packaging

## Performance, Budgets & Scaling

- Add per-graph time and memory budgets
- Add async and incremental generation to avoid stalls
- Add parallel evaluation of independent branches
- Add GPU acceleration of heavy nodes (noise, fields, scatter)
- Add caching and reuse of expensive intermediate results
- Add spatial chunking to bound working-set size
- Add level-of-detail generation for distant content
- Add quality presets scaling generation detail
- Add adaptive throttling from performance headroom
- Add memory budgets and eviction for generated data
- Add profiling and cost attribution per node and graph
- Add a headless generation benchmark harness
- Add machine-readable generation metrics for CI
- Add over-budget diagnostics with responsible nodes

## Testing & Validation

- Add determinism tests for a fixed seed
- Add cross-thread and cross-platform reproducibility tests
- Add node unit tests for each node type
- Add graph golden-output tests
- Add constraint-solver correctness tests
- Add reachability and connectivity tests for dungeons and cities
- Add seam-continuity tests across generated chunks
- Add attribute-schema validation tests
- Add regeneration-stability tests after edits
- Add streaming-generation integration tests
- Add large-scale generation performance stress tests
- Add memory-budget and eviction tests
- Add degenerate-output detection tests
- Add golden-image tests for representative generated scenes

# Capability 13 · Animation System

**Objective:** Deliver an end-to-end animation pipeline covering import, rigging, authoring, runtime evaluation, blending, constraints, retargeting, deformation, cinematics, debugging, compression, streaming, and scalable crowd execution.

## Skeleton & Rig

- Add a skeleton asset with a bone hierarchy
- Add per-bone reference (bind) pose transforms
- Add bone parent indices and traversal order
- Add inverse bind matrices for skinning
- Add named bones with stable identifiers
- Add bone groups and chains for tools
- Add sockets and attachment points on bones
- Add attach and detach of entities to sockets
- Add skeleton compatibility and remap metadata
- Add a standardized rig-mapping layer for cross-skeleton sharing
- Add bone display and gizmo metadata for the editor
- Add virtual and helper bones for tools
- Add per-bone constraints metadata
- Add skeleton validation (loops, missing parents, scale)
- Add skeleton import from asset formats
- Add skeleton versioning and migration

## Skinning & Deformation

- Add linear-blend skinning
- Add dual-quaternion skinning to reduce candy-wrapper artifacts
- Add optimized-center-of-rotation skinning for volume preservation
- Add per-vertex bone indices and weights
- Add a configurable maximum influences per vertex
- Add GPU skinning with a bone-matrix palette
- Add a CPU skinning fallback path
- Add a skin cache reused across passes
- Add skinning for depth, shadow, and velocity passes
- Add previous-frame skinned positions for motion vectors
- Add normal and tangent skinning
- Add non-uniform-scale handling
- Add delta-mush smoothing deformer
- Add tension and stretch-driven deformation
- Add corrective and pose-space deformers
- Add a skin-wrap deformer for proxy-driven meshes
- Add lattice and cage deformers
- Add blend-shape-plus-skin combined deformation
- Add deformer stacking with configurable order
- Add skinning and deformation validation and cost diagnostics

## Animation Clips & Data

- Add an animation clip asset with per-bone tracks
- Add position, rotation, and scale keyframes per bone
- Add scalar curve tracks for custom values
- Add configurable interpolation (linear, cubic, stepped)
- Add clip sampling at arbitrary time
- Add looping and clamping modes
- Add clip duration, frame rate, and playback range
- Add additive clips relative to a reference pose
- Add clip metadata (length, bone set, root motion presence)
- Add clip trimming, cropping, and time-warping
- Add clip concatenation and stitching
- Add per-clip event and marker tracks
- Add an animation library grouping many clips
- Add streaming of clip data on demand
- Add clip import from asset formats
- Add clip validation against a skeleton
- Add clip versioning and migration
- Add deterministic sampling for tests and networking

## Pose & Evaluation Core

- Add a pose representation in local bone space
- Add conversion between local and model space
- Add a pose blend primitive (linear interpolation)
- Add per-bone weighted blending
- Add additive pose application
- Add a reference-pose and identity-pose source
- Add a pose stack for layered evaluation
- Add an evaluation graph of pose-producing nodes
- Add lazy evaluation of only-needed bones
- Add a pose cache to reuse results within a frame
- Add thread-safe pose evaluation off the main thread
- Add scratch-buffer pooling for evaluation
- Add pose normalization and quaternion continuity
- Add bone-mask-aware evaluation
- Add evaluation ordering and dependency resolution
- Add pose-evaluation cost accounting

## Rigging Tools & Skeleton Authoring

- Add creation of skeletons and bones in the editor
- Add bone insert, delete, split, and merge
- Add bone rename, reparent, and reorder
- Add interactive bone placement in the viewport
- Add bone orientation and roll adjustment
- Add automatic bone-axis orientation
- Add chain creation for spines, limbs, and tails
- Add symmetry so edits mirror across an axis
- Add snapping of bones to mesh features
- Add joint gizmos and manipulators
- Add bone length, radius, and display shape controls
- Add markers for sockets and attachment points
- Add bone color, group, and layer organization
- Add a skeleton templates library (biped, quadruped, bird, custom)
- Add reference-mesh alignment guides
- Add validation and cleanup of the authored skeleton
- Add undo and redo across skeleton edits
- Add export of authored skeletons

## Skin Weighting & Deformation Authoring

- Add automatic weight binding on skin attach
- Add heat-map and geodesic-distance auto-weighting
- Add a weight-painting brush with add, subtract, and smooth
- Add per-bone weight visualization
- Add weight normalization and max-influence limiting
- Add weight mirroring across a symmetry axis
- Add weight smoothing, sharpening, and flooding
- Add weight copy and transfer between meshes
- Add weight pruning of tiny influences
- Add locking of specific bone weights while painting
- Add envelope and falloff-based weighting
- Add component and vertex selection for targeted editing
- Add weight editing by numeric entry and tables
- Add gradient and along-bone weighting tools
- Add a deformation preview while posing
- Add detection and fixing of unweighted vertices
- Add undo and redo across weight edits
- Add weighting validation and reports

## Auto-Rigging

- Add one-click rig generation for standard characters
- Add automatic joint placement from a mesh
- Add rig templates for biped and quadruped
- Add guided marker placement for auto-rig
- Add automatic control-rig generation on top of the skeleton
- Add automatic skin binding after auto-rig
- Add symmetry-aware auto-rigging
- Add finger, toe, and face auto-rig options
- Add scale and proportion adaptation to the mesh
- Add validation and a fix-up pass after auto-rig
- Add re-run of auto-rig preserving manual tweaks
- Add auto-rig presets and a beginner one-click path

## Animator Rig Controls

- Add authored control shapes bound to bones
- Add a control hierarchy separate from the deformation skeleton
- Add IK and FK controls with switching
- Add IK/FK matching to preserve pose on switch
- Add space switching for controls (world, parent, custom)
- Add pole-vector and aim controls
- Add custom control colors, shapes, and sizes
- Add a control picker UI for fast selection
- Add selection sets and control groups
- Add forward and backward rig solving
- Add secondary controls for offsets and tweaks
- Add attribute controls exposed on rig nodes
- Add a visual rig-graph for control logic
- Add reusable rig modules (arm, leg, spine, hand)
- Add rig mirroring and symmetry
- Add rig evaluation off the main thread
- Add baking of control-rig animation to bone keys
- Add importing control animation back onto the rig
- Add rig validation and cycle detection
- Add a rig-controls debug and display toggle

## Procedural & Runtime Constraint Rigging

- Add a runtime rig that applies constraints after animation
- Add aim, position, rotation, and scale constraints
- Add parent and multi-parent constraints with weights
- Add look-at chains for heads, spines, and tails
- Add spring and jiggle bones for secondary motion
- Add damped follow constraints
- Add distance and pole constraints
- Add driven bones (one bone drives another via curves)
- Add pose-driver (radial-basis) corrective poses
- Add corrective blend shapes driven by bone angles
- Add twist distribution along limbs
- Add bone-chain physics for cloth-like appendages
- Add constraint ordering and an evaluation stack
- Add per-constraint weight and blending
- Add runtime rig evaluation off the main thread
- Add rig debug visualization
- Add rig validation and cycle detection

## Keyframe Animation & Posing

- Add setting keyframes on bones and controls
- Add auto-key that records changes while posing
- Add key on selected, on all, and on modified channels
- Add a posing mode with interactive manipulators
- Add copy, paste, and mirror of poses
- Add a pose library with thumbnails
- Add applying and blending library poses by percentage
- Add holding, breakdown, and in-between key tools
- Add tween and favor tools between keys
- Add push, exaggerate, and dampen pose tools
- Add snapping controls to the ground and to targets
- Add pinning of effectors while posing
- Add symmetry posing across an axis
- Add selection sets for fast channel keying
- Add key deletion, insertion, and moving
- Add a playback and scrub bar with ranges and loop
- Add a sticky and editable current-frame value display
- Add pose reset to reference or to a stored pose
- Add undo and redo across posing
- Add deterministic authored output

## Curve & Graph Editor

- Add a curve editor showing animation channels
- Add editing of keys with position and value handles
- Add tangent types (auto, linear, flat, stepped, broken)
- Add tangent weighting and free handles
- Add ease-in and ease-out presets
- Add box and lasso selection of keys
- Add move, scale, and retime of key selections
- Add snapping to frames and value grids
- Add channel filtering and isolation
- Add curve smoothing, simplify, and resample filters
- Add a noise and jitter generator on curves
- Add pre- and post-infinity cycle modes
- Add copy and paste of curve segments
- Add a value ladder and numeric key entry
- Add multi-curve normalized view
- Add a read-only reference curve overlay
- Add undo and redo across curve edits
- Add a beginner-friendly simplified curve mode

## Dope Sheet & Timeline Editing

- Add a dope sheet showing keys per channel and object
- Add move, scale, and ripple edits of keys
- Add box selection and multi-object editing
- Add snapping, frame stepping, and key navigation
- Add summary tracks that aggregate child keys
- Add time-range selection and looping
- Add scaling of timing to change speed
- Add insert, delete, and shift of time
- Add key color-coding by channel type
- Add a synced current-frame indicator across editors
- Add marker and annotation tracks on the timeline
- Add undo and redo across timeline edits

## Non-Linear Animation & Authoring Layers

- Add non-linear clips arranged on tracks
- Add trim, slip, and time-scale of clips
- Add crossfade and blend between clips
- Add additive and override tracks
- Add authoring animation layers with weights
- Add per-layer bone masks
- Add reorder, solo, and mute of layers and tracks
- Add merging and flattening of layers to keys
- Add clip looping and hold on tracks
- Add transition clips with blend curves
- Add reuse of a clip in multiple places
- Add extraction of a sub-range into a new clip
- Add baking of the non-linear result to a single clip
- Add non-linear editing preview
- Add undo and redo across non-linear edits
- Add validation of track and layer coverage

## Animation Baking & Cleanup

- Add baking of simulation and constraints to keyframes
- Add baking of control-rig motion to bone keys
- Add plotting of a channel to dense keys
- Add resampling to a target frame rate
- Add an euler-filter to remove rotation flips
- Add key reduction with an error tolerance
- Add smoothing and noise-removal passes
- Add gap filling and hold cleanup
- Add root and pivot re-centering
- Add offset, scale, and time-shift of baked results
- Add bake ranges and selective channel baking
- Add non-destructive bake previews
- Add validation of baked output against the source
- Add batch baking across many clips

## Onion Skinning & Reference

- Add onion-skin ghosts of past and future frames
- Add configurable ghost count, spacing, and color
- Add per-object onion-skin toggles
- Add motion trails for selected controls
- Add editable motion trails that move keys in the viewport
- Add reference-video overlay in the viewport
- Add reference-image planes for posing
- Add a side-by-side reference playback panel
- Add annotation and grease-pencil sketching over frames
- Add capture of the current view as a reference

## Animation Graph & State Machines

- Add an animation state machine
- Add states that play clips or sub-graphs
- Add transitions with conditions and priorities
- Add transition blend durations and curves
- Add entry, default, and exit states
- Add any-state transitions
- Add nested and hierarchical sub-state machines
- Add transition interruption and re-entry rules
- Add conduits and shared transition logic
- Add graph parameters (float, int, bool, trigger, vector)
- Add parameter-driven conditions and expressions
- Add state entry, update, and exit callbacks
- Add automatic and time-based transitions
- Add transition blend by source and destination pose
- Add caching of pose results across the graph
- Add graph functions and reusable sub-graphs
- Add per-state playback speed and time scaling
- Add relevancy so inactive branches are skipped
- Add a data-driven graph asset format
- Add graph versioning and migration
- Add graph evaluation off the main thread
- Add deterministic graph evaluation for tests

## Blend Trees & Blend Spaces

- Add a 1D blend space driven by one parameter
- Add a 2D directional blend space for locomotion
- Add a 2D freeform blend space
- Add nested blend trees
- Add per-sample clip references and positions
- Add weighted N-way blending
- Add automatic weight computation from parameters
- Add blend smoothing and parameter damping
- Add per-sample playback-rate scaling for speed warping
- Add sync-group alignment across blended clips
- Add blend-space authoring with sample placement
- Add blend-space preview and grid visualization
- Add deterministic blend evaluation
- Add blend-space validation for coverage gaps

## Layered & Masked Blending

- Add animation layers evaluated in order
- Add per-layer weight control
- Add bone masks limiting a layer to a subset of bones
- Add override and additive layer modes
- Add per-bone blend weights within a mask
- Add smooth blend-in and blend-out of layers
- Add upper-body and lower-body split examples
- Add mask authoring with bone selection and falloff
- Add layer priority and conflict resolution
- Add masked additive layers for reactions and aiming
- Add per-layer sync options
- Add layer debug visualization

## Additive & Difference Animation

- Add additive-clip creation from a base and target pose
- Add reference-pose subtraction for difference clips
- Add additive blending onto a base pose
- Add additive weight and masking
- Add aim and lean additive layers
- Add breathing and idle-variation additives
- Add hit-reaction additives blended over locomotion
- Add additive-space validation
- Add additive preview in the editor

## Inverse Kinematics

- Add a two-bone IK solver
- Add a FABRIK chain solver
- Add a cyclic-coordinate-descent solver
- Add a look-at (aim) solver
- Add pole-vector control for elbow and knee direction
- Add foot placement IK aligned to ground
- Add ground-normal detection and foot roll
- Add hip and pelvis adjustment for foot IK
- Add hand IK for weapon and prop grips
- Add full-body IK with multiple effectors
- Add IK goals with position and rotation targets
- Add per-effector weight and blend
- Add joint limits and constraints
- Add stretch and squash limits per chain
- Add IK/FK blending
- Add solver iteration and tolerance controls
- Add stable and deterministic convergence
- Add IK on top of the animation graph output
- Add IK target authoring and runtime binding
- Add IK solver cost budgets
- Add IK debug visualization of goals and chains
- Add IK convergence validation

## Retargeting

- Add a standardized humanoid bone abstraction
- Add mapping from a skeleton to the abstraction
- Add retargeting of clips between compatible skeletons
- Add translation-retention rules per bone
- Add proportion and scale compensation
- Add pose-based retarget alignment (T-pose or A-pose)
- Add per-bone retarget mode (animation, skeleton, animation-scaled)
- Add root and pelvis retargeting for locomotion
- Add finger and face retargeting options
- Add live retargeting at runtime
- Add retargeting on import with baking
- Add interactive retarget-pose editing
- Add a chain and limb mapping editor
- Add retarget preview and side-by-side comparison
- Add retarget-profile assets reusable across characters
- Add batch retargeting of animation sets
- Add retargeting validation and mismatch reporting
- Add a mismatch fallback that preserves a usable pose
- Add retargeting between differing topologies (biped to quadruped hints)
- Add retargeting determinism for tests

## Root Motion & Motion Extraction

- Add root-motion extraction from clips
- Add application of root motion to the owning entity
- Add in-place playback that discards root motion
- Add root motion accumulation across a frame
- Add root motion from blended and layered sources
- Add root motion from the animation graph
- Add motion warping to hit precise targets
- Add curve-driven speed and direction adjustment
- Add root-motion and physics-controller reconciliation
- Add turn-in-place and pivot handling
- Add automatic root-bone detection and authoring
- Add extraction from a chosen bone or a virtual root
- Add networked root-motion synchronization
- Add root-motion debug visualization
- Add root-motion determinism for replay
- Add validation of extracted motion against clip data

## Motion Matching

- Add a motion database built from clips
- Add pose and trajectory feature extraction
- Add a feature schema (foot positions, velocities, trajectory)
- Add custom user-defined features
- Add nearest-match query against the database
- Add trajectory prediction from input
- Add blending into the selected pose
- Add cost weighting per feature
- Add tag and constraint filtering of candidates
- Add database compression and acceleration structures
- Add continuity and responsiveness tuning
- Add pose-history and inertia handling
- Add a fallback to graph-based animation
- Add authoring and preview of motion databases
- Add data-capture tooling to grow the database
- Add motion-matching debug visualization
- Add quality and cost scaling
- Add determinism for replay and tests

## Morph Targets & Blend Shapes

- Add a morph-target asset with per-vertex deltas
- Add weighted morph application
- Add combined skinning and morph deformation
- Add GPU morph evaluation
- Add sparse morph storage for efficiency
- Add many simultaneous active morphs
- Add morph normal and tangent deltas
- Add curve-driven and animation-driven morph weights
- Add corrective morphs driven by pose
- Add in-editor sculpting of morph shapes
- Add morph groups, presets, and combinations
- Add morph LOD reduction with distance
- Add morph import from asset formats
- Add morph validation against the mesh
- Add morph debug inspection

## Facial Animation & Lip Sync

- Add a facial rig built on bones or blend shapes
- Add a facial control board abstraction
- Add expression presets and combinations
- Add emotion blending and layering
- Add viseme and phoneme-driven lip sync
- Add audio-driven mouth animation
- Add text-to-viseme generation
- Add eye look-at, saccades, and blink systems
- Add tongue and jaw controls
- Add curve-driven facial control values
- Add corrective shapes for extreme expressions
- Add facial-animation retargeting between characters
- Add facial-capture input and cleanup
- Add a facial pose library
- Add facial preview and control UI
- Add facial-animation validation

## Physics-Based Animation

- Add ragdoll setup from the skeleton
- Add blend from animation to ragdoll on death or impact
- Add blend from ragdoll back to animation (get-up)
- Add partial ragdoll for reactive limbs
- Add physical animation that drives bones toward animated targets
- Add hit reactions blended over locomotion
- Add spring-bone secondary motion (hair, cloth, accessories)
- Add cloth-simulation handoff for skinned garments
- Add per-bone physics blend weights
- Add collision handling during physical animation
- Add impulse and force application to driven bones
- Add stability and damping controls
- Add ragdoll joint limits authored from the rig
- Add physics-animation determinism options
- Add physics-animation debug visualization
- Add integration with the physics module

## Cinematics & Sequence Editor Core

- Add a multi-track cinematic sequence asset
- Add tracks bound to entities, cameras, and properties
- Add animation clips placed on tracks
- Add transform and property tracks with keyframes
- Add blending and crossfades between clips on a track
- Add sub-sequences nested inside a sequence
- Add a master timeline with playback and scrubbing
- Add frame-accurate evaluation and looping ranges
- Add spawnable objects created and destroyed by the sequence
- Add possessable bindings to existing scene objects
- Add per-track mute, solo, and lock
- Add folders and grouping of tracks
- Add markers, chapters, and labeled ranges
- Add an event track that fires gameplay and script calls
- Add audio and dialogue tracks synced to the timeline
- Add material, light, and post-process parameter tracks
- Add a visibility track to show and hide objects
- Add time dilation and slow-motion tracks
- Add a data-driven sequence asset format
- Add sequence versioning and migration
- Add deterministic sequence evaluation
- Add sequence playback off the main thread where possible

## Cinematic Cameras

- Add a cinematic camera with lens and sensor settings
- Add focal length, aperture, and focus-distance controls
- Add depth-of-field and bokeh tied to camera settings
- Add camera rigs (dolly, crane, rail, tripod)
- Add a rail and spline-follow camera
- Add look-at and target-tracking constraints
- Add camera shake and handheld noise
- Add a camera cut track for switching cameras
- Add smooth blends between cameras
- Add virtual-camera framing guides and composition overlays
- Add camera bookmarks and saved framings
- Add gameplay-to-cinematic camera handoff
- Add auto-framing and follow behaviors
- Add lens presets and real-world camera matching
- Add safe-area, grid, and aspect-ratio overlays
- Add camera-path preview and visualization
- Add camera-animation baking and export
- Add multi-camera preview thumbnails

## Cutscene Authoring & Flow

- Add cutscene sequences triggered by gameplay
- Add trigger volumes and script hooks to start cutscenes
- Add skippable cutscenes with clean state handoff
- Add interactive cutscenes with input prompts
- Add branching cutscenes based on state
- Add gameplay-to-cutscene and cutscene-to-gameplay transitions
- Add character possession and control during cutscenes
- Add letterboxing and cinematic UI toggles
- Add subtitle and dialogue synchronization
- Add localization of cutscene audio and subtitles
- Add save and resume across cutscenes
- Add a cutscene director for orchestrating actors
- Add fallback handling when a bound actor is missing
- Add cutscene preview from any point
- Add cutscene validation of bindings and triggers
- Add a cutscene flow graph linking sequences

## Recording, Takes & Motion Capture

- Add recording of gameplay and simulation into clips
- Add a take system with multiple recorded versions
- Add take naming, metadata, and organization
- Add recording of transforms, properties, and audio
- Add live motion-capture input streaming
- Add mapping of capture data onto a rig
- Add mocap import from standard formats
- Add mocap cleanup (jitter, foot slide, gaps)
- Add foot-lock and contact fixing on captured data
- Add retargeting of captured motion to project skeletons
- Add facial and finger capture support
- Add layering of captured and hand-keyed animation
- Add a review workflow for takes
- Add baking of takes to clip assets
- Add capture-session management and calibration
- Add capture and take diagnostics

## Cinematic Rendering & Export

- Add high-quality cinematic rendering mode
- Add a render queue for sequences
- Add frame-sequence image export
- Add movie-file export
- Add resolution, frame-rate, and aspect controls
- Add anti-aliasing and sampling overrides for renders
- Add motion-blur accumulation for offline quality
- Add render passes and layers export
- Add burn-in of timecode and metadata
- Add deterministic rendering for consistent takes
- Add batch rendering of multiple sequences
- Add render progress, cancel, and diagnostics

## Runtime Playback & Control

- Add a play, stop, and pause API
- Add crossfade between clips and states
- Add one-shot playback over a base pose
- Add montage-style slotted playback with sections
- Add section jumping and looping within a slot
- Add blend-in and blend-out per playback request
- Add interruption and priority between requests
- Add playback speed and time-scale control
- Add reverse and ping-pong playback
- Add per-slot masking so slots affect chosen bones
- Add queued and sequenced playback
- Add pose snapshot and freeze
- Add scripting API for animation control
- Add gameplay-driven parameter updates
- Add completion and interruption callbacks
- Add deterministic playback for replay

## Animation Events & Markers

- Add event markers on clip timelines
- Add duration event ranges with begin and end
- Add firing of events during playback
- Add event routing to gameplay and scripts
- Add footstep, sound, and effect events
- Add events that survive blending and interruption
- Add event tracks in the clip editor
- Add typed event payloads
- Add event suppression during fast blends
- Add event debug logging and visualization
- Add deterministic event firing for tests

## Sync Groups & Phase

- Add sync markers on clips for phase alignment
- Add sync groups with a leader and followers
- Add phase matching across blended locomotion clips
- Add automatic leader selection by weight
- Add normalized-time synchronization
- Add stride and cadence matching
- Add sync across state transitions
- Add sync-group debug visualization
- Add sync determinism for tests

## Mirroring

- Add mirror data mapping left and right bones
- Add mirrored pose evaluation
- Add mirrored clip playback
- Add automatic mirror-mapping generation from naming
- Add mirror-aware curves and events
- Add per-axis mirror configuration
- Add mirror preview and validation

## Import & Interchange Pipeline

- Add skeleton import from standard asset formats
- Add clip import with track extraction
- Add morph-target import
- Add automatic tangent-space and bind-pose handling
- Add retargeting on import to a project skeleton
- Add compression settings applied on import
- Add root-motion extraction options on import
- Add import of multiple clips from one file
- Add naming and mapping conventions on import
- Add scene and camera-animation import for cinematics
- Add export of clips, rigs, and cameras to interchange formats
- Add round-trip round-tripping with external tools
- Add import validation and error reporting
- Add re-import that preserves overrides
- Add import presets per content source
- Add batch import and export
- Add import and export diagnostics and previews

## Animation Compression

- Add keyframe reduction with an error threshold
- Add curve fitting and resampling
- Add per-bone compression settings
- Add rotation quantization
- Add constant-track collapsing
- Add relative-error metrics against the source
- Add compression presets by content type
- Add streaming-friendly compressed layouts
- Add decompression cost budgets
- Add compression quality diagnostics
- Add per-platform compression targets
- Add compression validation against tolerance

## Animation Streaming & LOD

- Add streaming of clips and libraries on demand
- Add bone-LOD that evaluates fewer bones at distance
- Add animation update-rate reduction with distance
- Add off-screen and dormant animation pausing
- Add interpolation to hide reduced update rates
- Add crowd-friendly aggressive animation LOD
- Add per-character LOD bias and forced LOD
- Add shared evaluation for identical crowd poses
- Add residency budgets for animation data
- Add LOD integration with the mesh LOD system
- Add streaming and LOD diagnostics
- Add validation of correctness across animation LODs

## ECS Integration & Bulk Animation

- Store animation state as components in chunk storage
- Add parallel pose evaluation across the worker pool
- Add SIMD-vectorized bone-matrix computation
- Add SIMD-vectorized pose blending over chunks
- Add job-graph scheduling of sample, blend, IK, and skin stages
- Add bulk evaluation of large crowds
- Add shared clip and skeleton data across instances
- Add zero-copy handoff of bone matrices to GPU skinning
- Add memory-traffic-aware batch sizes for evaluation
- Add deterministic parallel evaluation across thread counts
- Add instanced-crowd pose sharing and variation
- Add scaling to thousands of animated characters within budget
- Add throughput diagnostics for bulk animation
- Add a crowd-animation stress harness

## 2D & Sprite Animation

- Add sprite-sheet and flipbook animation
- Add frame timing and looping control
- Add atlas import and slicing
- Add cutout and bone-based 2D animation
- Add 2D skeletal skinning of sprite meshes
- Add 2D IK for limbs
- Add 2D mesh deformation and weighting tools
- Add 2D animation events and markers
- Add a 2D animation timeline editor
- Add blending between 2D animations
- Add 2D animation preview

## User-Friendly Authoring

- Make the animation editor usable immediately with no setup
- Add a one-click auto-rig for imported characters
- Add ready-to-use starter rigs and animation sets
- Add drag-and-drop of animations onto characters
- Add a large-icon, plain-language tool palette
- Add sensible defaults that produce good motion instantly
- Add a beginner mode that hides advanced controls
- Add guided workflows for rig, skin, and animate
- Add a template gallery of characters, rigs, and animations
- Add one-click "make it loop" and "clean up" actions
- Add one-click retarget onto any compatible character
- Add live preview of every change
- Add always-available undo, redo, and autosave
- Add friendly warnings with one-click fixes
- Add a motion library with searchable presets
- Add plain-language sliders (speed, intensity, smoothness)
- Add tooltips, hints, and a short interactive tutorial
- Add crash-safe recovery of in-progress work
- Add a distraction-free posing mode
- Add graphics-tablet and touch support for posing

## Animation Editor Shell & Workspace

- Add a dockable animation workspace layout
- Add synchronized time across all editor panels
- Add a viewport with posing manipulators and gizmos
- Add a playback toolbar with ranges, loop, and speed
- Add switching between rig, animate, and cinematic modes
- Add a channel and object outliner
- Add customizable panels and saved layouts
- Add a graph, dope-sheet, and non-linear editor tab set
- Add copy and paste across editor panels
- Add global undo and redo across all animation tools
- Add editor templates and starting workspaces
- Add a searchable command and node reference

## Debugging & Visualization

- Add skeleton and bone-axis rendering
- Add current-pose visualization
- Add active-state and transition display for the graph
- Add blend-weight readouts per node and layer
- Add IK goal and chain visualization
- Add constraint and rig visualization
- Add event-firing timeline overlay
- Add motion-trail visualization
- Add root-motion path visualization
- Add per-node evaluation-cost display
- Add a live parameter inspector
- Add a graph step-through for a captured frame

## Performance & Budgets

- Add per-character evaluation budgets
- Add parallel evaluation of independent characters
- Add update-rate and LOD-driven cost scaling
- Add pooling of evaluation buffers
- Add caching of unchanged sub-graph results
- Add GPU offload of skinning and morph work
- Add crowd batching and shared evaluation
- Add profiling and cost attribution per stage
- Add a headless animation benchmark harness
- Add machine-readable animation metrics for CI
- Add over-budget diagnostics with responsible characters

## Testing & Validation

- Add clip-sampling correctness tests
- Add blend and layer correctness tests
- Add state-machine transition tests
- Add IK convergence and stability tests
- Add retargeting fidelity tests
- Add root-motion extraction and application tests
- Add morph and skinning correctness tests
- Add compression error-tolerance tests
- Add determinism tests across runs and thread counts
- Add event-firing correctness tests
- Add sequence evaluation and binding tests
- Add golden-pose regression tests
- Add crowd-animation performance stress tests
- Add import round-trip validation tests

## Animation Graph Node Library

- Add a sequence-player node
- Add a sequence-evaluator node driven by an explicit time
- Add a random-sequence player node
- Add a blend-space player node
- Add a blend-space evaluator node
- Add a two-way blend node
- Add a multi-way blend node
- Add a blend-by-boolean node
- Add a blend-by-enum node
- Add a blend-by-integer node
- Add an apply-additive node
- Add a mesh-space additive node
- Add a make-dynamic-additive node
- Add a layered-bone-blend node
- Add a blend-bone-by-channel node
- Add a copy-pose-from-another-mesh node
- Add a reference-pose and identity-pose node
- Add a modify-bone node
- Add a rotate-root-bone node
- Add a slot node for layered playback requests
- Add a cached-pose node with named references
- Add a sub-graph and linked-layer node
- Add a call-function node that invokes graph functions
- Add a modify-curve node
- Add per-node relevancy and short-circuiting

## Inertialization & Transition Blending

- Add inertialization for source-free transition blending
- Add dead-blending as an alternative smoothing method
- Add per-bone inertialization
- Add curve and attribute inertialization
- Add inertialization requests raised by transitions
- Add configurable inertialization duration and easing
- Add pose-snapshot capture and blend-from-snapshot
- Add handling of teleports and discontinuities
- Add inertialization interaction with additive layers
- Add inertialization cost budgets
- Add inertialization debug visualization

## Animation Warping

- Add stride warping to match foot speed to movement speed
- Add orientation warping to lean animation toward the movement direction
- Add slope warping to adapt legs and body to terrain incline
- Add motion warping that aligns root motion to target points
- Add named warp targets updated from gameplay
- Add warp windows scoped to time ranges in a clip
- Add per-warp masks and per-bone influence
- Add curve-driven warp strength
- Add warping applied on top of the graph output
- Add combination of multiple warps in a defined order
- Add stability and clamping to avoid over-warping
- Add warping determinism for replay
- Add warp-target and warped-pose debug visualization

## Motion Trajectory & Locomotion

- Add sampling of past motion history into a trajectory
- Add prediction of a future trajectory from input and velocity
- Add smoothing and resampling of trajectories
- Add distance-to-marker matching for starts, stops, and pivots
- Add distance matching that syncs animation time to travelled distance
- Add turn-in-place detection and playback
- Add stride adjustment tied to movement speed
- Add a locomotion helper library of common nodes
- Add automatic blend-space sample placement from root-motion analysis
- Add foot-lock curves that pin feet during ground contact
- Add automatic foot-sync-marker generation
- Add ground alignment combining foot IK and slope warping
- Add a locomotion state model (idle, start, loop, stop, pivot)
- Add trajectory feeding into the motion-matching query
- Add locomotion-authoring previews
- Add trajectory and locomotion debug visualization

## Contextual & Interaction Animation

- Add multi-actor synchronized interaction scenes
- Add role assignment for participants in an interaction
- Add alignment of participants to an interaction anchor
- Add per-role animation and warp points
- Add synchronized start and phase across participants
- Add branching and variant selection within an interaction
- Add interruption and early-exit handling
- Add entry and exit transitions to and from gameplay
- Add attachment and prop handling during interactions
- Add networked synchronization of interactions
- Add contextual-interaction authoring tools
- Add validation of participant compatibility
- Add interaction debug visualization

## Data-Driven Animation Selection

- Add selection tables that map conditions to animations
- Add typed input columns (state, tags, floats, enums)
- Add output columns for animations, blend spaces, and assets
- Add first-match and weighted selection modes
- Add fallback rows for unmatched conditions
- Add nested and chained selection tables
- Add runtime evaluation from graph and gameplay
- Add integration with the motion-matching database
- Add hot-reload of selection tables
- Add authoring UI for selection tables
- Add validation of coverage and conflicts
- Add selection debug output

## Pose Assets & Drivers

- Add a pose-asset holding named poses
- Add a pose-by-name evaluation node
- Add weighted blending of multiple named poses
- Add curve-driven pose weights
- Add a radial-basis pose driver from bone transforms
- Add corrective poses driven by joint angles
- Add facial and expression pose sets
- Add extraction of poses from animation frames
- Add pose-asset authoring and preview
- Add pose-asset validation against the skeleton
- Add pose-driver debug visualization

## Custom Attributes & Curve Pipeline

- Add scalar animation curves alongside bone tracks
- Add per-bone custom attributes on clips
- Add per-pose custom attributes carried through evaluation
- Add typed attribute values (float, int, vector, string)
- Add curve metadata and naming
- Add a curve-source interface for external drivers
- Add bulk curve storage and evaluation
- Add a time-stretch curve for non-uniform retiming
- Add attribute and curve blending across layers
- Add gameplay data authored on the animation timeline
- Add curve and attribute filters
- Add attribute and curve debug inspection

## Animation Modifiers & Asset Processing

- Add reusable animation modifiers applied to clips
- Add automatic foot-sync-marker generation
- Add automatic foot-lock curve generation
- Add motion-curve extraction (speed, direction, distance)
- Add root-motion generation and cleanup modifiers
- Add curve creation and remapping modifiers
- Add notification and event insertion modifiers
- Add batch application across many clips
- Add re-application on re-import
- Add a modifier library and custom modifiers
- Add preview of modifier results
- Add validation and reporting for modifiers

## GPU Deformer Graph & ML Deformation

- Add a node-based GPU mesh-deformation graph
- Add a linear-blend-skinning deformer node
- Add morph and blend-shape deformer nodes
- Add cloth and simulation read-back deformer nodes
- Add spline, lattice, and cage deformer nodes
- Add custom compute-kernel deformer nodes
- Add mesh-deformer hooks with geometry read-back
- Add per-LOD deformer configuration
- Add alternate skin-weight profiles per LOD
- Add a machine-learning deformer that approximates high-fidelity results
- Add a training pipeline for ML deformers
- Add a fallback to standard skinning where unsupported
- Add deformer-graph authoring and preview
- Add deformer cost budgets and diagnostics
- Add deformer-graph debug visualization

## Vertex Animation & Crowd Baking

- Add baking of skeletal animation into vertex-position textures
- Add baking of animated normals into textures
- Add playback of vertex-animation textures on instanced meshes
- Add crowd rendering driven entirely by baked animation
- Add per-instance time offset and animation variation
- Add blending between baked animation states
- Add sharing and instancing of animation across many crowd actors
- Add a distance transition from baked to fully skinned
- Add a bake pipeline with resolution and quality settings
- Add memory and texture budgets for baked animation
- Add validation of baked-animation fidelity
- Add crowd-animation debug visualization

## Live Animation Streaming

- Add a live data-source abstraction for external capture
- Add subjects for skeletons, cameras, faces, and transforms
- Add real-time streaming into the running engine
- Add mapping of live subjects onto project rigs
- Add retargeting of live data to project skeletons
- Add interpolation and latency compensation for live data
- Add timecode synchronization across subjects
- Add recording of live streams into clips and takes
- Add multiple simultaneous live sources
- Add live facial and finger capture
- Add a live preview in the editor and in play
- Add reconnection and dropout handling
- Add live-stream diagnostics
- Add validation of subject-to-rig mapping

## Sequence Editor Advanced Tracks & Bindings

- Add an attach track that parents an object over time
- Add a path-follow track along a spline
- Add a constraint track (aim, position, parent) over time
- Add a console-variable track driven by the sequence
- Add a streaming-level and data-layer visibility track
- Add camera-shake source and trigger tracks
- Add material-parameter-collection tracks
- Add custom-primitive-data tracks
- Add typed property tracks (bool, enum, int, float, vector, color, rotation, string, object reference)
- Add dynamic bindings resolved at runtime
- Add binding overrides per instance
- Add marked frames and time retiming
- Add per-track and per-section conditions and variants
- Add layered animation mixing within the sequence
- Add pose-search and motion-matching tracks

# Capability 14 · Physics / Simulation

**Objective:** Deliver stable and inspectable 3D and 2D physics foundations with engine-owned contracts for bodies, queries, contacts, constraints, characters, vehicles, deformables, simulation stepping, networking, authoring, and performance scaling.

## Physics Core & World

- Add a 3D physics world driven by the Jolt backend
- Add a 2D physics world driven by the Box2D backend
- Add a fixed-timestep simulation loop with an accumulator
- Add configurable sub-stepping per frame
- Add a spiral-of-death clamp on catch-up steps
- Add gravity configuration per world and per region
- Add multiple independent physics worlds
- Add per-scene physics world ownership and lifecycle
- Add pause, resume, and single-step of the simulation
- Add a global time-scale that slows or speeds simulation
- Add deterministic stepping with a fixed order
- Add world creation, reset, and teardown
- Add world configuration presets (arcade, realistic, precise)
- Add broadphase configuration and tuning
- Add solver iteration and accuracy settings
- Add world-level statistics (bodies, contacts, islands)
- Add a unified 2D/3D world interface where sensible
- Add world serialization for deterministic restarts

## Backend Integration & Abstraction

- Wrap the Jolt 3D backend behind an engine-facing interface
- Wrap the Box2D 2D backend behind an engine-facing interface
- Add typed handles for bodies, shapes, and constraints
- Add lifetime and ownership management of backend objects
- Add a capability query for backend-specific features
- Add graceful handling of features one backend lacks
- Add backend allocator routing through the memory tracker
- Add backend job/threading integration with the worker pool
- Add backend version pinning and upgrade notes
- Add a debug-draw bridge from each backend
- Add configuration mapping from engine settings to each backend
- Add error and assertion routing into engine diagnostics
- Add backend profiling hooks
- Add validation that engine and backend state stay consistent

## ECS Runtime Integration

- Add a rigid-body component bound to a backend body
- Add collider components mapped to backend shapes
- Add a constraint component bound to a backend joint
- Add a trigger/sensor component
- Add a character-controller component
- Add creation and destruction of backend objects from component lifecycle
- Add transform sync from physics to entity transforms
- Add transform sync from entities to kinematic bodies
- Add interpolation of render transforms between fixed steps
- Add a stable entity-to-body mapping in both directions
- Add deferred physics structural changes through the command buffer
- Add parallel readback of simulation results across chunks
- Add batched application of forces and impulses
- Add SIMD-friendly layouts for physics-adjacent components
- Add scheduling of the physics step within the system scheduler
- Add ownership of static-body creation for colliders without a rigid body
- Add dirty tracking so only changed bodies re-sync
- Add bulk spawn and despawn of physics entities
- Add streaming activation and deactivation of bodies per world cell
- Add validation of entity/body consistency each frame

## Rigid Bodies 3D

- Add dynamic rigid bodies
- Add static bodies
- Add kinematic bodies driven by animation or code
- Add automatic mass computation from shape and density
- Add manual mass, center of mass, and inertia overrides
- Add linear and angular damping
- Add per-body gravity scale
- Add velocity get and set (linear and angular)
- Add force, torque, and impulse application
- Add impulse at a world point
- Add position and rotation teleport with velocity handling
- Add sleeping and automatic wake on interaction
- Add sleep thresholds and manual sleep control
- Add continuous collision detection for fast bodies
- Add motion-quality selection (discrete vs continuous)
- Add per-axis position and rotation locks
- Add maximum velocity and angular-velocity clamps
- Add kinematic-to-dynamic and back transitions
- Add per-body user data linking to the entity
- Add mass and inertia debug readouts
- Add scaling handling for shapes and inertia
- Add body activation and deactivation control

## Colliders & Shapes 3D

- Add a box collider
- Add a sphere collider
- Add a capsule collider
- Add a cylinder collider
- Add a tapered-capsule and cone collider
- Add a convex-hull collider
- Add automatic convex-hull generation from a mesh
- Add a compound collider of multiple shapes
- Add a triangle-mesh collider for static geometry
- Add a heightfield collider for terrain
- Add a plane collider
- Add per-shape local transform offsets
- Add non-uniform scale handling per shape
- Add shape margins and skin width
- Add a simple-vs-complex collision distinction
- Add convex decomposition for concave meshes
- Add shape caching and reuse across bodies
- Add collider validation (degenerate, inverted, too-small)

## Physics Materials 3D

- Add a physics-material asset
- Add static and dynamic friction
- Add restitution (bounciness)
- Add friction and restitution combine modes
- Add per-shape material assignment
- Add per-triangle materials on mesh colliders
- Add surface-type tags for footsteps and effects
- Add density used for automatic mass
- Add rolling and spinning friction
- Add material validation and defaults

## Collision Filtering 3D

- Add collision layers and object types
- Add a layer-versus-layer collision matrix
- Add per-body include and exclude masks
- Add collision groups for pair suppression
- Add sub-group filtering for articulated bodies
- Add query-only and simulation-only filter distinctions
- Add trigger-versus-solid response configuration
- Add named layers authored as an asset
- Add editor UI for the collision matrix
- Add validation of filter configuration
- Add runtime changes to a body's filters
- Add filter debug visualization

## Scene Queries 3D

- Add ray casts returning the closest hit
- Add ray casts returning all hits
- Add sphere casts and swept-sphere queries
- Add box and capsule sweeps
- Add convex-shape sweeps
- Add overlap tests for a shape at a pose
- Add point-inside and closest-point queries
- Add filtered queries by layer, mask, and tag
- Add query flags (static only, dynamic only, triggers)
- Add hit results with point, normal, distance, and material
- Add hit results with the struck entity and shape index
- Add back-face and initial-overlap handling
- Add batched queries for many rays
- Add async query submission and retrieval
- Add a query cache for repeated identical queries
- Add deterministic query ordering
- Add a scripting API for all query types
- Add query debug visualization

## Collision Events & Triggers 3D

- Add contact-begin, contact-stay, and contact-end events
- Add trigger and sensor overlap begin and end events
- Add contact points, normals, and separation data
- Add contact impulse and relative-velocity data
- Add filtering of which pairs report events
- Add per-body enable of contact reporting
- Add routing of events to gameplay and scripts
- Add deferred event dispatch drained once per frame
- Add contact modification callbacks before the solver
- Add one-shot and continuous event modes
- Add threshold filtering by impulse for impact sounds
- Add stable pair identity across frames
- Add event payloads with both entities and shapes
- Add suppression of self-collision events
- Add deterministic event ordering
- Add contact and trigger debug visualization

## Constraints & Joints 3D

- Add a fixed constraint
- Add a point (ball-socket) constraint
- Add a hinge constraint with an axis
- Add a slider (prismatic) constraint
- Add a cone-twist constraint
- Add a six-degrees-of-freedom constraint
- Add a distance constraint with min and max
- Add a spring-damper constraint
- Add a gear constraint
- Add a pulley and rack constraint
- Add a path/rail constraint
- Add angular and linear limits per axis
- Add motors and drives with target position and velocity
- Add drive stiffness, damping, and force limits
- Add breakable constraints with force and torque thresholds
- Add break events routed to gameplay
- Add soft and hard constraint modes
- Add constraint frames and local anchors
- Add collision enable/disable between constrained bodies
- Add runtime enable, disable, and retarget of constraints
- Add constraint solver-iteration overrides
- Add constraint debug visualization

## Constraint Authoring & Tools

- Add a constraint editor with gizmos
- Add interactive anchor and axis placement
- Add limit and cone visualization while editing
- Add drive and motor tuning UI
- Add breakable-threshold setup and preview
- Add snapping of anchors to bones and features
- Add constraint presets (door, wheel, rope, chain)
- Add copy and mirror of constraints
- Add validation of constraint configuration
- Add live preview of constrained motion

## Character Controller / Movement 3D

- Add a kinematic capsule character controller
- Add ground detection and grounded state
- Add slope-limit handling and sliding on steep surfaces
- Add step-up and step-down over small obstacles
- Add automatic stair traversal
- Add ceiling detection and head bonk handling
- Add wall sliding along surfaces
- Add snap-to-ground to stay on slopes and stairs
- Add crouch with capsule resize and clearance checks
- Add pushing of dynamic rigid bodies
- Add being pushed by moving and kinematic bodies
- Add riding of moving platforms with inherited velocity
- Add rotating-platform support
- Add collide-and-slide movement resolution
- Add penetration recovery and depenetration
- Add configurable skin width and contact offset
- Add a dynamic-body character mode as an alternative
- Add root-motion-driven movement reconciliation
- Add external forces and impulses on the controller
- Add gravity and custom up-vector support
- Add velocity, acceleration, and speed queries
- Add ground-normal and surface-type readback
- Add a scripting API for controller movement
- Add controller debug visualization

## Character Movement Modes 3D

- Add walking and running with acceleration curves
- Add jumping with variable height
- Add falling with air control
- Add coyote time and jump buffering
- Add swimming with buoyancy and drag
- Add flying and no-clip modes
- Add climbing and ledge handling
- Add mantling and vaulting helpers
- Add sprint, dash, and dodge helpers
- Add slope-speed adjustment
- Add configurable movement presets
- Add networked movement synchronization
- Add movement-mode transition events
- Add movement debug readouts

## Ragdoll 3D

- Add ragdoll body and constraint generation from a skeleton
- Add a physics-asset describing bodies, shapes, and joints
- Add automatic capsule fitting per bone
- Add joint-limit authoring per bone
- Add self-collision configuration
- Add activation of ragdoll on death or impact
- Add blend from animation into ragdoll
- Add blend from ragdoll back into animation (get-up)
- Add partial ragdoll for reactive limbs
- Add powered ragdoll driven toward an animated pose
- Add impulse application for hit reactions
- Add pose readback from ragdoll to the skeleton
- Add ragdoll sleeping and settling
- Add ragdoll LOD and dormancy at distance
- Add ragdoll authoring and preview tools
- Add ragdoll validation against the skeleton

## Cloth Simulation

- Add a cloth component with a simulation mesh
- Add particle and distance-constraint cloth solving
- Add bending and shear constraints
- Add pinning and attachment to bones and bodies
- Add wind and force response
- Add collision against capsules, spheres, and planes
- Add collision against the character body
- Add self-collision
- Add tearing and breakable cloth
- Add per-vertex stiffness and mass painting
- Add a paint tool for constraints and colliders
- Add cloth LOD and distance-based simplification
- Add GPU cloth solving where available
- Add skinned-to-cloth blend on the same mesh
- Add cloth-to-render-mesh skinning
- Add wind-source integration from the weather system
- Add cloth sleeping when at rest
- Add cloth authoring and preview
- Add cloth determinism options
- Add cloth cost budgets and diagnostics

## Destruction & Fracture

- Add a fracture authoring tool for meshes
- Add Voronoi-based fracturing
- Add clustered and hierarchical fracture levels
- Add a connection graph between chunks
- Add break-on-impact from contact impulse
- Add break-on-force and stress thresholds
- Add damage accumulation and propagation
- Add partial breakage revealing interior faces
- Add interior-material assignment on fracture
- Add debris spawning as rigid bodies
- Add debris lifetime, budgets, and cleanup
- Add radial and directional break forces
- Add force fields affecting broken pieces
- Add anchoring so structures stay until enough support breaks
- Add structural-support collapse simulation
- Add pre-fractured asset caching for performance
- Add runtime fracture for dynamic cuts
- Add streaming and pooling of debris
- Add destruction events routed to gameplay
- Add destruction LOD and distance culling
- Add fracture preview and tuning
- Add destruction determinism options

## Vehicle Physics

- Add a wheeled-vehicle simulation
- Add per-wheel suspension with spring and damper
- Add wheel raycast or shapecast ground contact
- Add tire friction with a friction model
- Add longitudinal and lateral slip
- Add an engine model with torque curve
- Add a gearbox with automatic and manual modes
- Add a clutch and drivetrain
- Add a differential (open, locked, limited-slip)
- Add steering with Ackermann geometry
- Add brakes and a handbrake
- Add downforce and aerodynamic drag
- Add anti-roll bars
- Add tracked-vehicle (tank) support
- Add motorcycle and two-wheeled balance
- Add wheel visual sync and steering animation
- Add surface-dependent traction
- Add vehicle reset and recovery
- Add vehicle input API and script control
- Add networked vehicle synchronization
- Add vehicle telemetry output
- Add vehicle debug visualization

## Vehicle Authoring & Tuning

- Add a vehicle setup asset
- Add wheel placement and configuration tools
- Add suspension and tire tuning UI
- Add engine and gearbox tuning UI
- Add a center-of-mass adjustment tool
- Add vehicle presets (sports car, truck, offroad, kart)
- Add live tuning while driving
- Add a telemetry graph panel
- Add validation of vehicle configuration
- Add a one-click drivable-vehicle setup

## Soft Body Simulation

- Add a soft-body volume simulation
- Add tetrahedral or shape-matching deformation
- Add pressure and volume preservation
- Add stiffness, damping, and plasticity controls
- Add collision with rigid bodies and the environment
- Add self-collision for soft bodies
- Add pinning and attachment points
- Add tearing and breaking of soft bodies
- Add skinning of a render mesh to the soft body
- Add soft-body LOD and simplification
- Add GPU soft-body solving where available
- Add soft-body authoring and preview
- Add soft-body sleeping and budgets
- Add soft-body determinism options

## Fluid Simulation

- Add a particle-based (SPH) fluid simulation
- Add configurable viscosity, density, and surface tension
- Add fluid containers and boundaries
- Add fluid interaction with rigid bodies and buoyancy
- Add fluid emitters and drains
- Add foam, spray, and bubble generation
- Add surface reconstruction for rendering
- Add fluid collision with the environment
- Add two-way coupling with rigid bodies
- Add grid-based fluid as an alternative solver
- Add GPU fluid solving where available
- Add flow, current, and force fields on fluid
- Add fluid LOD and particle budgets
- Add integration with the water surface system
- Add fluid authoring and preview
- Add fluid determinism options

## Particle Physics Simulation

- Add physics-driven particles with collision
- Add gravity, drag, and force response for particles
- Add particle-to-world collision with bounce and friction
- Add particle-to-particle interaction where affordable
- Add spawn from emitters with initial velocity
- Add lifetime, budgets, and pooling
- Add force-field and wind response
- Add GPU particle-physics solving
- Add handoff to and from the visual-effects system
- Add sub-stepping for fast particles
- Add particle-physics debug visualization
- Add particle-physics determinism options

## Forces, Fields & Effectors

- Add force regions applying directional force
- Add radial explosion forces with falloff
- Add wind zones affecting physics bodies
- Add buoyancy volumes with fluid density
- Add area gravity and gravity overrides
- Add area linear and angular damping
- Add vortex and turbulence fields
- Add drag and resistance volumes
- Add attractor and repulsor fields
- Add conveyor and surface-velocity effectors
- Add one-way and directional pass-through volumes
- Add field composition and priority
- Add impulse-on-enter and continuous-force modes
- Add scripting hooks for custom forces
- Add force-region authoring tools
- Add force and field debug visualization

## Rigid Bodies 2D

- Add dynamic 2D rigid bodies via Box2D
- Add static 2D bodies
- Add kinematic 2D bodies
- Add automatic mass from shape and density
- Add manual mass, center of mass, and inertia
- Add linear and angular damping
- Add per-body gravity scale
- Add velocity get and set
- Add force, torque, and impulse application
- Add fixed-rotation and locked-axis options
- Add bullet mode (continuous collision) for fast bodies
- Add sleeping and wake control
- Add teleport with velocity handling
- Add per-body user data linking to the entity
- Add body activation and deactivation
- Add 2D body debug readouts

## Colliders & Shapes 2D

- Add a box (polygon) collider
- Add a circle collider
- Add a capsule collider
- Add a convex-polygon collider
- Add an edge collider
- Add a chain collider for level boundaries
- Add a compound collider of multiple fixtures
- Add per-fixture local offsets
- Add one-way (platform) collision
- Add collider radius and skin controls
- Add automatic collider generation from sprites
- Add automatic collider generation from outlines
- Add shape caching and reuse
- Add 2D collider validation

## Joints 2D

- Add a revolute joint
- Add a prismatic joint
- Add a distance joint
- Add a weld joint
- Add a pulley joint
- Add a gear joint
- Add a motor joint
- Add a wheel joint for vehicles
- Add a friction joint
- Add a spring-damper joint
- Add a mouse/target joint for dragging
- Add joint limits and motors
- Add breakable 2D joints with events
- Add collision enable between jointed bodies
- Add runtime joint changes
- Add 2D joint debug visualization

## Scene Queries 2D

- Add 2D ray casts with closest and all hits
- Add 2D shape casts and sweeps
- Add 2D overlap tests
- Add 2D point queries
- Add AABB region queries
- Add filtered 2D queries by layer and mask
- Add hit results with point, normal, and fraction
- Add hit results with the struck entity and fixture
- Add batched 2D queries
- Add a scripting API for 2D queries
- Add deterministic 2D query ordering
- Add 2D query debug visualization

## Collision Events & Triggers 2D

- Add 2D contact begin, stay, and end events
- Add 2D sensor overlap events
- Add contact points, normals, and impulses in 2D
- Add pre-solve and post-solve callbacks
- Add per-fixture event enable
- Add routing of 2D events to gameplay and scripts
- Add deferred 2D event dispatch
- Add impulse-threshold filtering in 2D
- Add stable 2D pair identity
- Add self-collision suppression in 2D
- Add deterministic 2D event ordering
- Add 2D contact debug visualization

## Effectors & Areas 2D

- Add an area (buoyancy) effector
- Add a point effector with attraction and repulsion
- Add a platform effector for one-way and side-friction control
- Add a surface effector for conveyor motion
- Add a directional and constant-force effector
- Add a drag and damping area
- Add gravity overrides per area
- Add effector falloff and masks
- Add effector composition and priority
- Add effector authoring tools
- Add effector scripting hooks
- Add effector debug visualization

## 2D Character & Platformer Movement

- Add a 2D character-body controller
- Add ground, wall, and ceiling detection
- Add slope handling and slope-limit
- Add one-way platform drop-through
- Add moving-platform riding
- Add ladder and rope climbing
- Add coyote time and jump buffering in 2D
- Add variable jump height and double jump
- Add wall slide and wall jump
- Add dash and dodge helpers
- Add a 2D movement scripting API
- Add 2D controller debug visualization

## Tilemap & 2D World Physics

- Add collision generation from tilemaps
- Add composite and merged collider generation
- Add per-tile collision shapes and one-way flags
- Add automatic rebuild of tilemap collision on edit
- Add per-tile physics materials
- Add streaming of tilemap physics with the world
- Add optimization of large tilemap colliders
- Add tilemap collision debug visualization
- Add validation of generated tilemap collision
- Add one-way and platform tiles

## Simulation Stepping & Determinism

- Add a fixed-timestep step decoupled from frame rate
- Add sub-stepping with a maximum per frame
- Add interpolation of transforms between steps
- Add extrapolation as an alternative to interpolation
- Add deterministic body and contact ordering
- Add deterministic solver configuration
- Add seed control for any stochastic behavior
- Add cross-platform determinism validation
- Add async physics on a dedicated thread
- Add a synchronization point for gameplay reads
- Add rewind and re-simulation support
- Add snapshot and restore of full physics state
- Add step-cost budgets and adaptive sub-stepping
- Add determinism diagnostics and drift detection
- Add fixed-point option evaluation for strict determinism
- Add per-world stepping isolation

## Networking & Replication

- Add replication of rigid-body state
- Add client-side prediction of physics
- Add server-authoritative reconciliation
- Add snapshot interpolation for remote bodies
- Add deterministic lockstep simulation
- Add rollback and re-simulation on correction
- Add priority and relevancy for replicated bodies
- Add bandwidth-aware state compression
- Add ownership transfer of physics objects
- Add networked constraint and joint state
- Add networked destruction and break events
- Add anti-cheat validation of physics state
- Add networked-physics diagnostics
- Add replication tests across latency and loss

## Continuous Collision & Stability

- Add swept continuous collision for fast bodies
- Add speculative contacts to prevent tunneling
- Add penetration recovery with a bias
- Add contact-offset and skin-width tuning
- Add stacking stability tuning
- Add solver-iteration configuration for accuracy
- Add warm-starting of the solver
- Add restitution and friction stability at low speed
- Add jitter reduction and rest thresholds
- Add large-mass-ratio handling
- Add stability diagnostics and warnings
- Add stress-test scenes for stacking and chains

## Authoring & Editor Tools

- Add interactive collider editing with gizmos
- Add box, sphere, capsule, and hull fitting to a mesh
- Add one-click auto-collider generation
- Add convex-decomposition tooling with previews
- Add a collision-matrix and layer editor
- Add a physics-material editor and library
- Add ragdoll and physics-asset setup tools
- Add a vehicle setup workflow
- Add cloth and soft-body painting tools
- Add fracture authoring and preview
- Add drag-in-play manipulation of bodies
- Add a measure and mass-inspection tool
- Add snapping of colliders to geometry
- Add copy, mirror, and reuse of physics setups
- Add validation and fix-up of physics setups
- Add presets for common object types
- Add undo and redo across physics authoring
- Add a physics-setup gallery to learn from

## Debugging & Visualization

- Add collider wireframe rendering
- Add contact-point and normal visualization
- Add velocity and force vector visualization
- Add constraint and joint visualization
- Add sleeping and active-state coloring
- Add center-of-mass and inertia visualization
- Add query ray and sweep visualization
- Add trigger and overlap highlighting
- Add broadphase and island visualization
- Add a physics statistics HUD
- Add a per-body inspector
- Add a pause-and-step debugger for simulation
- Add slow-motion inspection
- Add a contact and event log
- Add capture and replay of a physics frame
- Add a screenshot-friendly clean physics overlay

## Performance, Budgets & Scaling

- Add a multi-threaded broadphase
- Add island-based parallel solving
- Add worker-pool integration for the solver
- Add sleeping and dormancy to skip idle bodies
- Add distance-based physics LOD and deactivation
- Add streaming activation of bodies per world cell
- Add per-frame simulation time budgets
- Add adaptive sub-stepping under load
- Add body and contact count budgets with warnings
- Add memory budgets and diagnostics for physics
- Add bulk-friendly data layouts for large body counts
- Add profiling and cost attribution per phase
- Add a headless physics benchmark harness
- Add machine-readable physics metrics for CI
- Add scaling to tens of thousands of bodies within budget
- Add over-budget diagnostics with responsible objects

## User-Friendly Authoring

- Make adding a collider and rigid body work with zero tuning
- Add automatic sensible mass and material defaults
- Add one-click ragdoll from a character
- Add one-click drivable vehicle from a mesh
- Add plain-language presets (bouncy, heavy, floaty, sturdy)
- Add auto-collider fitting on import
- Add friendly warnings with one-click fixes
- Add a beginner mode that hides advanced tuning
- Add live preview of physics behavior in the editor
- Add drag-and-drop physics presets
- Add guided setup for cloth, vehicles, and destruction
- Add a gallery of ready physics setups to reuse

## Testing & Validation

- Add rigid-body integration and determinism tests
- Add mass and inertia computation tests
- Add collision-shape correctness tests
- Add scene-query correctness tests for 3D
- Add scene-query correctness tests for 2D
- Add collision and trigger event tests
- Add constraint and joint stability tests
- Add breakable-constraint threshold tests
- Add character-controller movement tests (slopes, steps, platforms)
- Add 2D platformer movement tests
- Add ragdoll setup and blend tests
- Add cloth simulation and collision tests
- Add destruction and fracture tests
- Add vehicle simulation tests
- Add soft-body and fluid tests
- Add force-region and effector tests
- Add tilemap collision-generation tests
- Add ECS transform-sync and runtime-integration tests
- Add cross-platform determinism tests
- Add networked-physics prediction and reconciliation tests
- Add stacking and stability stress tests
- Add large-scale performance stress tests
- Add memory-budget and leak tests
- Add golden-scenario regression tests

## Collision Detection Pipeline

- Add a broadphase using an AABB tree
- Add a dirty-grid incremental broadphase for dynamic bodies
- Add a bounding-volume-hierarchy acceleration option
- Add a brute-force and particle-pair broadphase for small scenes
- Add narrowphase contact-generation dispatch
- Add a midphase for triangle-mesh contacts
- Add GJK distance queries between convex shapes
- Add EPA penetration-depth resolution
- Add SAT contact generation for boxes and convex shapes
- Add per-shape-pair contact-point generators
- Add contact-manifold generation and reduction
- Add redundant-contact pruning
- Add speculative contacts to prevent tunneling
- Add bit-based collision filtering in the broadphase
- Add incremental refit as bodies move
- Add collision-detection debug visualization

## Solver Internals & Islands

- Add a position-based-dynamics rigid solver
- Add a temporal Gauss-Seidel solver option
- Add island partitioning of interacting bodies
- Add parallel island grouping across the worker pool
- Add constraint-graph coloring for parallel-safe solving
- Add configurable position and velocity iteration counts
- Add warm-starting of the solver from the previous frame
- Add a moving simulation-space (relative-frame) mode
- Add structure-of-arrays body storage for cache efficiency
- Add deterministic body and constraint ordering
- Add sleeping and waking per island
- Add per-island solver-iteration overrides
- Add a solver validation and A/B harness
- Add solver-internals diagnostics

## Advanced Colliders & Implicit Shapes

- Add level-set (signed-distance) volumetric colliders
- Add skinned-mesh-driven colliders that deform with animation
- Add skinned level-set colliders
- Add implicit-shape union composition
- Add implicit-shape scaled and transformed wrappers
- Add implicit-shape intersection composition
- Add a bounding hierarchy over composed implicits
- Add runtime welding of bodies into one simulated proxy
- Add splitting of merged bodies back apart
- Add per-piece convex collision generation
- Add cooked collision-data caching
- Add tapered-capsule and tapered-cylinder shapes
- Add optional neural signed-distance colliders
- Add advanced-collider validation and preview

## Collision Channels, Presets & Profiles

- Add object channels describing what a body is
- Add trace channels for queries
- Add per-channel response (block, overlap, ignore)
- Add named collision presets combining object type and responses
- Add a collision-profile asset
- Add distinct query and simulation filtering
- Add per-shape channel overrides
- Add default presets for common object types
- Add a collision-preset editor
- Add validation of preset and channel setup
- Add migration of collision settings across versions
- Add a collision-response matrix visualization

## Physics Fields

- Add a physics-field system evaluated over bodies
- Add radial falloff fields
- Add plane and box falloff fields
- Add noise fields
- Add wave fields with configurable wave functions
- Add uniform and radial vector fields
- Add linear-force and linear-impulse targets
- Add angular-torque and angular-velocity targets
- Add linear-velocity and initial-velocity targets
- Add position and position-target drivers
- Add dynamic-state targets that switch kinematic and dynamic
- Add activation and disable-threshold targets
- Add sleeping-threshold targets
- Add kill and cull targets
- Add internal- and external-cluster-strain targets for fracture
- Add field operators (add, multiply, divide, subtract)
- Add field filters by object type and group
- Add field presets, authoring, and debug visualization

## Physics Interaction Components

- Add a grab handle that moves a body toward a target transform
- Add configurable handle stiffness, damping, and limits
- Add a spring component connecting two bodies
- Add a thruster component applying continuous force
- Add a radial-force and radial-impulse component
- Add an attractor and repulsor component
- Add a constraint component authored on entities
- Add a pickup-and-carry helper for gameplay
- Add a throw helper with inherited velocity
- Add a physics grab-and-manipulate gameplay helper
- Add a scripting API for interaction components
- Add interaction-component debug visualization

## Fracture Patterns & Authoring Tools

- Add uniform Voronoi fracturing
- Add custom Voronoi with user-placed sites
- Add radial and impact fracture patterns
- Add grid slice fracturing
- Add brick and bond patterns
- Add a single planar cut
- Add cutting by an arbitrary mesh
- Add automatic clustering into a hierarchy
- Add cluster-magnet and manual cluster tools
- Add a proximity and connection graph for connectivity
- Add anchoring so supported pieces stay in place
- Add embedding of geometry into the fracture hierarchy
- Add per-piece convex collision generation
- Add cleanup tools (fix tiny geometry, resample, recompute normals, auto-UV)
- Add a fracture-authoring mode with previews
- Add interior-material assignment on fracture

## Flesh & Muscle Simulation

- Add a tetrahedral FEM deformable simulation
- Add corotated and Neo-Hookean material models
- Add muscle activation with fiber directions
- Add a tetrahedral collection asset
- Add binding of skin to the deformable volume
- Add anatomical fat, muscle, and bone layering
- Add collision of flesh with rigid bodies
- Add self-collision for flesh
- Add stiffness, damping, and incompressibility controls
- Add flesh authoring and preview
- Add flesh LOD and cost budgets
- Add flesh determinism options

## Physics-Based Character Movement

- Add a dynamic-body character driven by physics
- Add a character-ground constraint for slope and step handling
- Add physics-based walking with acceleration and friction
- Add physics-based falling with air control
- Add physics-based flying and swimming modes
- Add crouch, jump, land, and launch handling
- Add water-entry detection and buoyant swimming
- Add ground-versus-air state detection
- Add path-following (spline, point, and route)
- Add reaction to impacts and external forces
- Add pushing and being pushed by dynamic bodies
- Add moving-platform support for physics characters
- Add a modular movement-mode framework with transitions
- Add networked movement prediction and reconciliation
- Add a choice between kinematic and physics character modes
- Add physics-character debug visualization

## Physical Animation Control

- Add physical-animation control driving bones toward an animated pose
- Add per-body and per-limb drive strength
- Add named control profiles (relaxed, braced, hit-reaction)
- Add blend weights between animation and physics per body
- Add smooth ramping of control strength over time
- Add impulse-driven hit reactions layered on animation
- Add masking so only chosen limbs go physical
- Add spring and damping control per body
- Add a control record for gameplay-driven changes
- Add a physics-with-control animation node
- Add integration with the animation graph output
- Add a runtime API for control profiles
- Add control-profile authoring and preview
- Add physical-animation-control debug visualization

## Physics Caching & Playback

- Add recording of physics simulation to a cache
- Add playback of a physics cache
- Add scrubbing and seeking within a cache
- Add caching of destruction and cloth results
- Add a cache asset format
- Add streaming of large caches
- Add blending from cache playback into live simulation
- Add deterministic re-recording
- Add cache compression
- Add interchange import and export of caches
- Add cache authoring and preview
- Add cache validation and diagnostics

## Buoyancy & Water Physics

- Add multi-point pontoon buoyancy on rigid bodies
- Add buoyancy sampled from the water surface
- Add a batched buoyancy manager for many floaters
- Add water drag and damping on submerged bodies
- Add water-current and flow forces on bodies
- Add floating stability and self-righting
- Add wave-driven bobbing tied to the water system
- Add splash and impact response entering water
- Add boat, raft, and buoy helpers
- Add enter-water and exit-water events
- Add buoyancy authoring and tuning
- Add buoyancy debug visualization

## Physics Recording & Visual Debugger

- Add recording of full physics state each frame
- Add replay and scrubbing of recorded physics
- Add inspection of bodies, shapes, and transforms in a recording
- Add inspection of contacts and contact points
- Add inspection of constraints and joints
- Add inspection of islands and solver state
- Add query and event overlays in the recording
- Add headless capture of physics recordings
- Add sharing and loading of recordings
- Add comparison of two recordings
- Add filtering and search within a recording
- Add export of recordings for bug reports

## Modular Vehicle Assembly

- Add component-based modular vehicle assembly
- Add an engine module with a torque curve
- Add clutch and transmission modules
- Add wheel and suspension modules
- Add aerofoil and downforce modules
- Add thruster and propulsion modules
- Add a chassis module with merged-body support
- Add wiring of modules into a drivetrain
- Add runtime attach and detach of modules
- Add per-module tuning and telemetry
- Add modular-vehicle presets
- Add modular-vehicle validation

# Capability 15 · Input / Audio / Media

**Objective:** Deliver production input, audio, video, voice, haptics, and media services with cross-platform device handling, authoring workflows, accessibility, streaming, synchronization, diagnostics, and bounded real-time performance.

## Input Core & Devices

- Add a device abstraction over all input hardware
- Add keyboard device support
- Add mouse device support with buttons, movement, and wheel
- Add gamepad device support with buttons, sticks, and triggers
- Add touchscreen device support
- Add pen and stylus support with pressure and tilt
- Add motion-sensor support (accelerometer, gyroscope)
- Add XR controller device support
- Add device hotplug detection and reconnection
- Add per-device state snapshots each frame
- Add both event-driven and polled access
- Add raw and processed value access
- Add device capability queries
- Add device naming, ids, and product metadata
- Add unified button, axis, and vector value types
- Add timestamps on input events
- Add per-platform device backends behind the abstraction
- Add device debug inspection

## Action Mapping & Bindings

- Add named input actions with typed values
- Add action maps grouping related actions
- Add mapping contexts with priority stacking
- Add binding an action to multiple devices and keys
- Add composite bindings (2D vector from keys, 1D axis)
- Add control schemes per device class
- Add automatic scheme switching on device use
- Add context push, pop, and enable/disable
- Add per-context consumption so higher contexts block lower
- Add action phases (started, ongoing, performed, canceled)
- Add action value queries and event callbacks
- Add chorded and modifier-key bindings
- Add binding groups for platform-specific sets
- Add data-driven action and binding assets
- Add runtime creation and modification of bindings
- Add validation of action and binding configuration
- Add versioning and migration of input assets
- Add a scripting API for actions and values

## Input Processing

- Add deadzone processing for sticks and triggers
- Add sensitivity and scaling processors
- Add invert and clamp processors
- Add normalization and vector-magnitude processors
- Add smoothing and acceleration processors
- Add a press interaction
- Add a hold interaction with duration
- Add a tap and multi-tap interaction
- Add a slow-tap and long-press interaction
- Add a chord interaction
- Add input buffering with a configurable window
- Add repeat and echo handling
- Add custom processor and interaction plugins
- Add ordering of processors in a chain
- Add per-binding processor overrides
- Add processing debug visualization

## Rebinding & Accessibility

- Add runtime interactive rebinding
- Add listen-for-next-input capture
- Add conflict detection and resolution
- Add cancel and reset-to-default rebinding
- Add save and load of custom bindings
- Add binding presets and profiles
- Add exclusion of reserved keys from rebinding
- Add hold-versus-toggle accessibility options
- Add remapping for one-handed and alternative layouts
- Add input-assist options (auto-run, sticky modifiers)
- Add per-action sensitivity and deadzone in settings
- Add a rebinding UI with live capture
- Add validation and warnings for unbound actions
- Add accessibility presets

## Touch & Gestures

- Add multi-touch point tracking
- Add touch begin, move, stationary, and end phases
- Add tap and double-tap gestures
- Add long-press gestures
- Add swipe and fling gestures with direction
- Add pinch and zoom gestures
- Add rotate gestures
- Add pan and drag gestures
- Add gesture recognizers with priorities
- Add on-screen virtual joysticks
- Add on-screen virtual buttons and d-pads
- Add customizable on-screen control layouts
- Add touch-to-action binding
- Add touch debug visualization

## XR & Motion Controllers

- Add XR controller pose tracking
- Add XR controller buttons, triggers, and thumbsticks
- Add hand-tracking joint poses
- Add gesture recognition for hands
- Add gaze and head-pose input
- Add grip and aim pose distinction
- Add XR haptic output
- Add binding of XR input to actions
- Add controller model and pointer visualization
- Add interaction rays and selection
- Add XR input capability negotiation
- Add XR input debug visualization

## Haptics & Force Feedback

- Add gamepad rumble with low and high motors
- Add trigger haptics where supported
- Add haptic patterns and envelopes
- Add per-device haptic capability queries
- Add playback, layering, and stop of haptics
- Add intensity scaling and user settings
- Add XR controller haptics
- Add spatialized and directional haptics
- Add haptic asset authoring
- Add haptics debug and test tools

## Local Multiplayer & Device Assignment

- Add multiple local users
- Add device pairing and assignment per user
- Add join and leave flows for local players
- Add per-user action state and contexts
- Add device reassignment and reconnection handling
- Add split-screen input routing
- Add a lobby-style press-to-join flow
- Add per-user binding profiles
- Add unpaired-device handling
- Add local-user debug inspection

## Input Runtime & Integration

- Add input polling and dispatch within the frame loop
- Add window and application focus handling
- Add input consumption and priority between UI and gameplay
- Add routing of input to the UI system first
- Add input components in the ECS for gameplay
- Add per-frame action snapshots for systems
- Add fixed-step input sampling for deterministic gameplay
- Add input recording and playback
- Add input injection for automated testing
- Add a scripting API for input queries and events
- Add pause-and-resume of input capture
- Add input flushing on context changes
- Add latency measurement of input to action
- Add input event logging and diagnostics

## Input User-Friendly & Editor

- Add an action and binding editor
- Add a visual device-and-binding map
- Add live input preview and testing
- Add ready-made control-scheme presets
- Add automatic detection of the active device for prompts
- Add device-appropriate button glyphs and prompts
- Add plain-language binding labels
- Add a beginner-friendly default setup that just works
- Add warnings for missing or conflicting bindings
- Add a gallery of example input setups

## Audio Engine Core

- Add an audio engine on the miniaudio backend
- Add a dedicated audio mixing thread
- Add configurable sample rate and buffer size
- Add output-device selection and enumeration
- Add device hotplug and default-device following
- Add a master output with volume and mute
- Add a voice pool with a configurable limit
- Add voice priority and stealing
- Add virtualization of inaudible voices
- Add revival of virtual voices when audible again
- Add per-voice state (playing, paused, stopped, virtual)
- Add sample-accurate scheduling
- Add resampling for mismatched sample rates
- Add channel-count handling (mono, stereo, surround)
- Add clock and timeline for synchronized playback
- Add glitch and underrun detection
- Add engine start, stop, and reset
- Add thread-safe command submission to the audio thread
- Add memory accounting for audio
- Add engine statistics (active voices, CPU, memory)

## Sound Sources & Playback

- Add one-shot sound playback
- Add looping sound playback
- Add 2D non-spatialized sources
- Add 3D spatialized sources
- Add play, stop, pause, and resume
- Add per-source volume and pitch
- Add fade-in and fade-out
- Add start offset and seeking
- Add per-source priority
- Add playback rate and time-stretch
- Add randomized pitch and volume variation
- Add sound variation sets with weighting
- Add follow-entity sources that track a transform
- Add one-shot fire-and-forget helpers
- Add loop points and sustain regions
- Add playback completion callbacks and events
- Add source pooling and reuse
- Add source debug inspection

## Mixing & Buses

- Add a hierarchy of mixer buses and groups
- Add submixes feeding parent buses
- Add per-bus volume, pitch, and mute
- Add solo and bypass per bus
- Add send and return routing
- Add side-chain ducking (music under dialogue)
- Add mixer snapshots and transitions
- Add category buses (music, sfx, dialogue, ambience, ui)
- Add user volume settings per category
- Add automatic ducking rules
- Add metering per bus
- Add real-time parameters driving mix values
- Add a data-driven mixer asset
- Add mixer validation
- Add mixer authoring and preview
- Add mixer state save and restore

## 3D / Spatial Audio

- Add distance attenuation with configurable curves
- Add min and max distance and rolloff models
- Add stereo and surround panning by direction
- Add doppler-effect pitch shifting
- Add source spread and size
- Add a listener driven by the active camera
- Add multiple listeners for split-screen
- Add listener weighting for nearest-listener audio
- Add HRTF binaural spatialization
- Add ambisonic encoding and decoding
- Add ambient and directional bed handling
- Add air-absorption filtering over distance
- Add early reflections approximation
- Add velocity tracking for doppler
- Add per-source spatialization toggles
- Add attenuation-shape authoring (sphere, cone, box)
- Add spatialization debug visualization
- Add near-field and focus handling

## DSP & Effects

- Add a reverb effect
- Add a parametric equalizer
- Add a compressor and limiter
- Add a delay and echo effect
- Add low-pass, high-pass, and band-pass filters
- Add distortion and saturation
- Add chorus and flanger
- Add pitch shifting and formant control
- Add effect chains per source and per bus
- Add effect bypass and wet/dry mix
- Add real-time parameter control of effects
- Add a custom-DSP plugin interface
- Add effect ordering and routing
- Add effect presets
- Add convolution reverb from impulse responses
- Add a spectrum analyzer and metering
- Add effect cost budgets
- Add effect authoring and preview

## Occlusion & Propagation

- Add occlusion tests between source and listener
- Add obstruction handling for partial blocking
- Add low-pass filtering driven by occlusion
- Add reverb zones with blending
- Add portal-based sound propagation between rooms
- Add geometry-based acoustic approximation
- Add dynamic reverb from the surrounding space
- Add material-based absorption and transmission
- Add diffraction approximation around edges
- Add propagation cost budgets and throttling
- Add async occlusion queries
- Add reverb-zone authoring
- Add propagation debug visualization
- Add integration with the physics query system

## Audio Data & Banks

- Add an audio-clip asset with format metadata
- Add compressed and uncompressed clip support
- Add streaming of large clips from disk
- Add async decode off the audio thread
- Add sound banks grouping related clips
- Add bank load and unload on demand
- Add reference counting of loaded audio
- Add preloading and warmup of critical sounds
- Add per-platform compression and quality settings
- Add loudness normalization metadata
- Add memory budgets and residency for audio
- Add clip import and conversion
- Add clip validation
- Add streaming and bank diagnostics

## Interactive Music & Ambience

- Add layered music with independently controlled stems
- Add music transitions synced to bars and beats
- Add stingers and one-shot musical accents
- Add tempo, beat, and bar tracking
- Add horizontal re-sequencing of music segments
- Add vertical layering driven by gameplay intensity
- Add crossfades and quantized switches
- Add ambient beds with looping textures
- Add randomized ambient one-shots with timing rules
- Add ambience zones with blending
- Add real-time parameters driving music and ambience
- Add a music-state and transition graph
- Add music authoring and preview
- Add music and ambience validation

## Audio Middleware Integration

- Add an abstraction for external audio middleware
- Add loading of middleware banks
- Add posting of middleware events
- Add real-time parameters passed to middleware
- Add switches and states for middleware
- Add listener and source registration with middleware
- Add a fallback to the built-in engine when middleware is absent
- Add middleware profiling and diagnostics
- Add capability negotiation with middleware
- Add memory and voice accounting through middleware
- Add a consistent gameplay API across built-in and middleware
- Add validation of middleware integration

## Audio Runtime & Integration

- Add audio update within the frame loop
- Add a listener bound to the active camera
- Add audio-source components in the ECS
- Add transform-driven 3D source positions
- Add pause of gameplay audio on focus loss
- Add a scripting API for playback and parameters
- Add animation-event-driven sound triggers
- Add physics-impact-driven sound triggers
- Add occlusion updates throttled per frame
- Add voice and CPU budgets enforced at runtime
- Add time-scale and pause interaction with audio
- Add save and restore of audio settings
- Add runtime device-change handling
- Add audio runtime diagnostics

## Audio Authoring & User-Friendly

- Add a mixer editor with buses and sends
- Add an attenuation-curve editor
- Add one-click 3D sound setup with good defaults
- Add drag-and-drop sound assignment to objects
- Add plain-language controls (loudness, distance, echo)
- Add live preview and audition of sounds
- Add ready-made sound and mix presets
- Add a beginner mode hiding advanced routing
- Add warnings for clipping and missing sounds
- Add a sound-effect library browser
- Add sensible defaults so audio works immediately
- Add a gallery of example audio setups

## Video Decoding & Formats

- Add a video decoder abstraction
- Add support for common container formats
- Add support for common video codecs
- Add hardware-accelerated decoding where available
- Add a software-decode fallback
- Add decoding on a background thread
- Add frame queueing and pacing
- Add color-space conversion to renderable formats
- Add decode error handling and recovery
- Add decode performance diagnostics
- Add per-platform decoder backends
- Add decoder capability queries

## Media Playback & Control

- Add play, pause, stop, and resume
- Add seeking to a timestamp
- Add looping and playback-rate control
- Add rendering video to a texture
- Add rendering video into UI widgets
- Add rendering video onto materials and surfaces
- Add synchronized audio-track playback
- Add audio/video sync correction
- Add multiple simultaneous video players
- Add buffering and preload control
- Add playback state events and callbacks
- Add end-of-media and error events
- Add frame-accurate stepping
- Add a scripting API for media control

## Streaming Video & Subtitles

- Add streaming playback from local files
- Add streaming playback from network sources
- Add adaptive-bitrate handling for network streams
- Add buffering and rebuffering handling
- Add subtitle and caption rendering
- Add multiple subtitle tracks and selection
- Add multiple audio tracks and selection
- Add localization of subtitles
- Add subtitle timing and styling
- Add closed-caption accessibility support
- Add stream error recovery
- Add streaming diagnostics

## Cutscene & UI Video Integration

- Add a fullscreen movie player
- Add skippable video playback
- Add pre-rendered cutscene playback
- Add background and looping UI video
- Add video as an animated UI element
- Add transitions into and out of video
- Add input handling during video (skip, pause)
- Add handoff between video and gameplay
- Add video-in-world screens and displays
- Add cutscene-video validation

## Voice Capture & Encoding

- Add microphone capture
- Add capture-device selection and enumeration
- Add configurable sample rate and frame size
- Add Opus encoding of captured audio
- Add voice-activity detection
- Add push-to-talk mode
- Add open-mic mode with a threshold
- Add noise suppression
- Add echo cancellation
- Add automatic gain control
- Add input-level metering and monitoring
- Add a local test-your-mic loopback
- Add capture start, stop, and mute
- Add capture device hotplug handling
- Add capture diagnostics
- Add per-user capture settings

## Voice Transmission & Channels

- Add network transmission of encoded voice
- Add voice channels and rooms
- Add team and proximity channels
- Add positional (spatial) voice by speaker location
- Add proximity attenuation for spatial voice
- Add per-channel join and leave
- Add speaker priority and ducking of others
- Add mute and block per speaker
- Add bandwidth-aware bitrate adaptation
- Add packet-loss concealment
- Add server relay and peer-to-peer options
- Add integration with the networking layer
- Add transmission diagnostics
- Add per-channel policy configuration

## Voice Playback & Mixing

- Add per-speaker voice playback
- Add a jitter buffer for smooth playback
- Add spatialization of positional voice
- Add mixing of voice with game audio
- Add ducking of game audio under voice
- Add per-speaker volume and mute
- Add a speaking indicator and levels
- Add voice routing to the mixer buses
- Add late-packet and dropout handling
- Add playback diagnostics
- Add accessibility options for voice
- Add per-user playback settings

## Voice Moderation & Safety

- Add local mute and block lists
- Add server-side mute and ban
- Add reporting of abusive voice
- Add transcription hooks for moderation
- Add profanity and safety filtering hooks
- Add parental controls and age-gating
- Add default-safe settings for minors
- Add consent and privacy handling for capture
- Add audit logging for moderation actions
- Add moderation diagnostics

## Performance & Budgets

- Add a voice budget for concurrent audio
- Add DSP-cost budgets and throttling
- Add streaming and decode budgets for audio and video
- Add input-to-action latency measurement
- Add audio-thread load monitoring
- Add video-decode cost monitoring
- Add voice-chat bandwidth and CPU monitoring
- Add per-system profiling and attribution
- Add a headless media benchmark harness
- Add machine-readable media metrics for CI
- Add memory budgets across input, audio, and media
- Add over-budget diagnostics

## Testing & Validation

- Add action-mapping and binding tests
- Add rebinding and conflict-resolution tests
- Add interaction and processor tests
- Add touch-gesture recognition tests
- Add local-multiplayer device-assignment tests
- Add deterministic input recording and playback tests
- Add audio mixing and routing tests
- Add 3D spatialization and attenuation tests
- Add DSP-effect correctness tests
- Add occlusion and propagation tests
- Add audio streaming and bank tests
- Add interactive-music transition tests
- Add video-decode and playback tests
- Add subtitle and track-selection tests
- Add voice capture, encode, and transmit tests
- Add voice spatialization and mixing tests
- Add latency and budget stress tests
- Add cross-platform device and format tests

## Procedural Audio & Node-Based Sound Graphs

- Add a node-based procedural audio graph
- Add a runtime graph execution engine for audio
- Add a playable procedural-audio source asset
- Add reusable sub-patches and presets
- Add runtime and scripted graph construction
- Add oscillator nodes (sine, saw, square, additive, supersaw)
- Add noise nodes (white, Perlin, low-frequency)
- Add filter nodes (low-pass, high-pass, band-pass, band-split, dynamic)
- Add envelope nodes (attack-decay, ADSR, follower)
- Add delay nodes (mono, stereo, grain, tap)
- Add reverb and diffuser nodes
- Add distortion, bitcrusher, and waveshaper nodes
- Add modulation nodes (ring mod, flanger, chorus, phaser)
- Add pitch-shift and Doppler nodes
- Add panner, mid/side, and crossfade nodes
- Add sample-and-hold and mixer nodes
- Add trigger and control nodes (sequence, repeat, counter, gate, compare, select)
- Add music-theory nodes (note-to-frequency, note quantizer, scale-to-array, tempo-to-seconds)
- Add graph input and output parameters exposed to gameplay
- Add a wave-writer node for capturing output
- Add a node library with search and metadata
- Add graph authoring, preview, and debugging

## Sound Cue Graphs, Classes & Submixes

- Add a sound-cue graph assembling sounds from nodes
- Add random, weighted, and shuffle selection nodes
- Add concatenate and sequence nodes
- Add mixer and layering nodes
- Add modulator nodes (pitch, volume, continuous)
- Add attenuation and distance-crossfade nodes
- Add branch, switch, and quality-level nodes
- Add delay and looping nodes
- Add a sound-class hierarchy for grouped control
- Add runtime sound mixes that adjust classes
- Add a submix graph with sends and returns
- Add per-submix effect chains
- Add cue templates and presets
- Add sound-cue authoring and preview
- Add sound-cue validation

## Audio Modulation & Control Buses

- Add audio control buses
- Add modulation patches driving parameters
- Add bus mixes that combine modulation sources
- Add modulation destinations (volume, pitch, effect parameters)
- Add curve-shaped modulation response
- Add real-time parameter routing from gameplay
- Add layered and priority modulation
- Add modulation presets
- Add modulation authoring and preview
- Add modulation debug inspection

## Real-Time Audio Analysis

- Add real-time loudness and broadcast-loudness metering
- Add a real-time spectrum analyzer
- Add constant-Q log-frequency analysis
- Add onset and beat detection
- Add peak and RMS metering
- Add both real-time and offline analysis paths
- Add analysis output routed to gameplay and visuals
- Add music-reactive parameter feeds
- Add per-source and per-submix analysis
- Add analysis widgets (meter, oscilloscope, spectrum)
- Add analysis cost budgets
- Add analysis debug visualization

## Sample-Accurate Music Clock & Quantization

- Add a sample-accurate musical clock
- Add a configurable metronome with tempo and meter
- Add quantized event scheduling to beats and bars
- Add beat-locked playback of sounds and music
- Add a quantized command queue on the audio thread
- Add game-thread notifications on beats and bars
- Add multiple synchronized clocks
- Add tempo changes and ramps
- Add quantization boundaries (beat, bar, phrase)
- Add clock debug readouts

## Procedural Synthesis & Motor Synth

- Add a subtractive virtual-analog synth component
- Add a granular-synthesis component
- Add a wavetable-synth component
- Add a tone and test-signal generator
- Add polyphony and voice management for synths
- Add MIDI and parameter control of synths
- Add a procedural engine/motor synthesizer driven by RPM
- Add grain-table authoring for motor synthesis
- Add load, throttle, and gear response for motor synth
- Add synth presets
- Add synth authoring and preview
- Add synth cost budgets

## Procedural Ambience & Soundscape

- Add a data-driven procedural ambience system
- Add placement points and palettes for ambient sounds
- Add rules for density, spacing, and timing of one-shots
- Add layered ambient beds with blending
- Add zone-driven ambience selection
- Add time-of-day and weather-driven ambience
- Add randomized non-repeating playback
- Add soundscape authoring and preview
- Add soundscape budgets and diagnostics
- Add soundscape debug visualization

## Spatialization Plugins, Soundfield & External Routing

- Add a spatialization plugin interface
- Add binaural HRTF spatialization backends
- Add a soundfield (ambisonic) format
- Add ambisonic encoding and decoding
- Add soundfield endpoints and rendering
- Add an occlusion and reverb plugin interface
- Add routing of audio into external engines at the mixer boundary
- Add offline and faster-than-real-time audio rendering
- Add platform spatial-audio endpoint support
- Add plugin capability negotiation and fallback
- Add spatialization debug visualization

## Interactive Music & MIDI

- Add MIDI file import and parsing
- Add a music map with tempo and timeline
- Add a MIDI clock synchronized to the music clock
- Add a MIDI-driven sampler with stems
- Add interactive stem mixing by intensity
- Add quantized transitions between music sections
- Add stingers and fills triggered on beats
- Add a music sequencer for interactive scores
- Add real-time parameters driving the score
- Add MIDI-controller input to music
- Add interactive-music authoring and preview
- Add interactive-music validation

## Audio Gameplay Volumes & Reverb Zones

- Add audio gameplay volumes that override audio in a region
- Add per-volume reverb settings
- Add per-volume occlusion and attenuation overrides
- Add per-volume submix routing
- Add blending between overlapping volumes
- Add priority and layering of volumes
- Add interior and exterior transitions
- Add modular audio gameplay components
- Add volume authoring tools
- Add volume debug visualization

## Media Framework, Sources & Playlists

- Add a media-player facade over multiple backends
- Add media sources for files, streams, and platform inputs
- Add time-synchronizable media sources
- Add a media texture for rendering video
- Add a media sound component for video audio
- Add media playlists with ordering and looping
- Add a media clock and track model
- Add multiple simultaneous media players
- Add pluggable platform and codec backends
- Add hardware-accelerated video decoders
- Add image-sequence and high-dynamic-range frame playback
- Add GPU tiled and mipped frame playback for large plates
- Add media events and state callbacks
- Add a media preview and inspection tool
- Add media-framework diagnostics

## Professional Video I/O & Timecode

- Add a capture-card abstraction for professional video
- Add serial-digital video capture and output
- Add network video input and output
- Add genlock synchronization
- Add timecode sources and synchronization
- Add multi-source timecode alignment
- Add frame-accurate capture and output
- Add color-space and format conversion for I/O
- Add audio embedding and de-embedding with video
- Add hardware capability detection
- Add a professional-I/O configuration UI
- Add professional-I/O diagnostics

## Game Capture, Encoding & Frame Streaming

- Add a hardware video and audio encoder abstraction
- Add capture of the game framebuffer to encoded video
- Add capture of game audio synchronized to video
- Add replay and highlight export to video files
- Add configurable resolution, bitrate, and codec
- Add a rendered-frame streaming pipeline over the network
- Add remote input injection from streamed clients
- Add cloud and browser play of streamed frames
- Add low-latency streaming transport
- Add adaptive quality for streamed frames
- Add multiple concurrent streaming sessions
- Add a frame-capture pipeline feeding encoders and streaming
- Add screenshot and clip-capture helpers
- Add capture and streaming budgets
- Add capture and streaming diagnostics

## Advanced Input: Device Properties, UI Navigation & External Devices

- Add a device-property system for output effects
- Add per-device LED color control
- Add adaptive-trigger resistance and effects
- Add spatialized and parameterized force feedback
- Add curve- and buffer-driven haptic effects
- Add player-mappable input configurations
- Add input injection for automation and replay
- Add a query for which contexts and keys map to an action
- Add a UI input-routing and focus-navigation layer
- Add gamepad, mouse, and touch input-method switching
- Add an action bar and activatable widget input stack
- Add directional UI navigation
- Add an external input-device plugin interface
- Add MIDI-controller input
- Add remote-control protocol bindings
- Add advanced-input debug tooling

# Capability 16 · UI / Text / Localization / Accessibility

**Objective:** Deliver a complete runtime and editor UI stack with predictable layout and rendering, robust input and focus, international text, localization, accessibility semantics, reusable authoring, testing, and scalable batching.

## UI Core & Widget Framework

- Add a retained-mode widget tree with a hierarchy
- Add widget lifecycle (construct, mount, update, unmount, destroy)
- Add parent/child slots and slot properties
- Add per-widget visibility modes (visible, hidden, collapsed, hit-test-invisible)
- Add enable and disable states
- Add render transform, opacity, and pivot per widget
- Add z-order and layering within a parent
- Add invalidation of layout and paint on change
- Add a dirty-tracking and rebuild pipeline
- Add widget identity and stable ids
- Add a widget registry and factory
- Add clipping and content culling
- Add hit-testing against the widget tree
- Add per-widget user data linking to gameplay
- Add a data-driven widget asset format
- Add widget asset versioning and migration
- Add a scripting API to build and query widgets
- Add widget-tree debug inspection

## Layout System

- Add a canvas panel with absolute positioning and anchors
- Add anchor points and offsets relative to the parent
- Add a horizontal and vertical box (stack) panel
- Add a grid panel with rows and columns
- Add a uniform grid panel
- Add a wrap panel that flows children
- Add an overlay panel that stacks children
- Add a scroll box with vertical and horizontal scrolling
- Add a size box with fixed and min/max sizing
- Add a spacer and separator
- Add a border and padding container
- Add margins, padding, and content alignment
- Add horizontal and vertical alignment per slot
- Add fill, auto, and fixed sizing rules
- Add aspect-ratio and constraint containers
- Add a two-pass measure-and-arrange layout
- Add flow direction (left-to-right and right-to-left)
- Add responsive breakpoints and adaptive layouts
- Add a safe-zone-aware layout container
- Add layout debug visualization

## UI Rendering & Draw

- Add a draw-element list per frame
- Add batching of draw elements by material and texture
- Add brushes for solid color, image, and nine-slice
- Add gradient and rounded-rectangle brushes
- Add custom material support on widgets
- Add clipping regions and scissor rectangles
- Add retained-mode caching of static sub-trees
- Add invalidation panels for partial redraw
- Add blur and backdrop effects
- Add per-widget custom draw callbacks
- Add render-target rendering of widgets
- Add draw-call and batch statistics
- Add pixel-snapping for crisp edges
- Add UI rendering debug overlay

## Input, Focus & Interaction

- Add mouse input (move, buttons, wheel, hover)
- Add touch input with multi-touch
- Add keyboard input and shortcuts
- Add gamepad navigation and activation
- Add a focus system with a focused-widget path
- Add directional navigation between widgets
- Add tab-order navigation
- Add event bubbling and tunneling
- Add pointer capture and release
- Add hover, press, and release states
- Add drag-and-drop with payloads and drop targets
- Add gesture recognition (tap, long-press, swipe, pinch)
- Add input routing between UI and gameplay
- Add input consumption and pass-through rules
- Add focus-visible indicators
- Add on-screen virtual cursor for gamepad
- Add repeat and hold input handling
- Add interaction debug visualization

## Widget Library — Common Controls

- Add a button with click and hold events
- Add a text label
- Add an image and icon widget
- Add a checkbox
- Add a radio button group
- Add a toggle switch
- Add a slider
- Add a range (dual-handle) slider
- Add a progress bar
- Add a spinner and busy indicator
- Add a single-line text field
- Add a multi-line text area
- Add a password field
- Add a numeric spin box
- Add a dropdown and combo box
- Add a list view with selection
- Add a tile and grid view
- Add a tree view
- Add a data table with columns
- Add a scrollbar
- Add a tab control
- Add an accordion and expander
- Add a menu bar and submenus
- Add a context menu
- Add a tooltip
- Add a modal dialog and message box
- Add a window and panel container
- Add a breadcrumb and pagination control

## Advanced & Composite Widgets

- Add a color picker
- Add a date and time picker
- Add a virtualized list for very large data sets
- Add a carousel and page viewer
- Add dockable and resizable panels
- Add a tree-table hybrid
- Add a property grid
- Add chart and graph widgets
- Add a minimap widget
- Add a radial and pie menu
- Add an on-screen virtual keyboard
- Add a rich item card widget
- Add a tag and chip input
- Add a split-view with draggable dividers

## Data Binding & View Models

- Add one-way data binding to widget properties
- Add two-way binding for input widgets
- Add view models separating data from presentation
- Add property-change notification
- Add collection and list binding with change tracking
- Add value converters and formatters in bindings
- Add command binding for actions
- Add binding to gameplay and entity data
- Add binding expressions and paths
- Add fallback and default values in bindings
- Add binding validation and error reporting
- Add lazy and batched binding updates
- Add debounced bindings for frequent changes
- Add a binding editor in the designer
- Add binding debug inspection
- Add compiled bindings for performance

## Styling & Theming

- Add reusable style assets for widgets
- Add per-state styles (normal, hover, pressed, disabled, focused)
- Add themes grouping styles and colors
- Add color schemes and palettes
- Add typography scales and text styles
- Add design tokens for spacing, radius, and color
- Add style inheritance and overrides
- Add light and dark theme variants
- Add runtime theme switching
- Add per-widget style overrides
- Add nine-slice and stateful brush styling
- Add a style editor with live preview
- Add high-contrast theme variants
- Add theme validation and coverage checks
- Add import and export of themes
- Add a default theme that looks good out of the box

## UI Animation & Transitions

- Add widget animation timelines
- Add property tweens (position, size, opacity, color, rotation)
- Add easing curves and custom curves
- Add enter and exit animations
- Add state-transition animations
- Add sequenced and parallel animations
- Add looping and ping-pong animations
- Add animation events and callbacks
- Add play, pause, reverse, and scrub control
- Add data-driven and scripted animations
- Add a timeline editor for widget animations
- Add reduced-motion respect in animations
- Add animation performance budgets
- Add animation preview in the designer

## Visual UI Designer

- Add a WYSIWYG design canvas
- Add drag-and-drop placement of widgets
- Add a hierarchy outliner of the widget tree
- Add a property panel for the selected widget
- Add live preview with sample data
- Add multi-resolution and device preview
- Add alignment, snapping, and distribution guides
- Add rulers, grid, and measurement overlays
- Add copy, paste, and duplicate of widgets
- Add undo and redo across design edits
- Add reusable component creation from a selection
- Add a component and template library
- Add a binding editor integrated in the designer
- Add a style and theme editor
- Add an animation timeline editor
- Add responsive-layout editing with breakpoints
- Add a preview of different cultures and text lengths
- Add an accessibility preview and checker
- Add zoom, pan, and isolation of sub-trees
- Add a widget palette with categories and search
- Add starter templates for menus, HUDs, and dialogs
- Add a gallery of example UIs to open and learn from

## UserWidget Authoring & Reusable Components

- Add user-defined composite widgets
- Add named content slots for injected children
- Add exposed properties on custom widgets
- Add exposed events and callbacks
- Add default values and property metadata
- Add nesting of custom widgets
- Add widget variants and overrides
- Add a reusable-component library
- Add instancing with per-instance overrides
- Add versioning and migration of custom widgets
- Add validation of component interfaces
- Add packaging and sharing of components
- Add live-reload of edited widgets
- Add documentation and previews for components

## UI Logic & Scripting

- Add event handlers bound to widget events
- Add binding of buttons and inputs to gameplay actions
- Add a scripting API for widget logic
- Add visual scripting nodes for UI logic
- Add UI state machines for screen flow
- Add conditions and data-driven visibility
- Add timers and delays in UI logic
- Add navigation and routing between screens
- Add form validation and submission logic
- Add localized dynamic content in logic
- Add UI-logic debugging and step-through
- Add deterministic UI logic for tests

## HUD & Game UI Layers

- Add a HUD system rendered over the game
- Add UI layers with ordering (background, HUD, menus, overlays, tooltips)
- Add a screen stack with push and pop
- Add modal and blocking-screen management
- Add screen transitions and fades
- Add world-to-screen markers and waypoints
- Add floating damage and status numbers
- Add health, resource, and status bars
- Add a minimap and radar
- Add notification and toast system
- Add an objective and quest tracker UI
- Add an inventory and menu framework
- Add pause-menu and settings-menu scaffolding
- Add per-player HUD for local multiplayer

## World-Space & 3D UI

- Add widgets rendered on surfaces in the world
- Add interaction with world-space widgets via rays and cursors
- Add curved and cylindrical UI surfaces
- Add depth, occlusion, and sorting for world UI
- Add billboard and face-camera UI
- Add XR and VR UI panels and pointers
- Add hand and controller interaction with UI
- Add gaze and dwell selection
- Add distance-based scaling and fading
- Add world-UI performance budgets
- Add stereo-correct UI rendering
- Add world-UI debug visualization

## UI Scaling, DPI & Safe Area

- Add DPI awareness and per-monitor scaling
- Add a reference resolution and scale rules
- Add resolution-independent layout units
- Add safe-area handling for notches and rounded corners
- Add TV overscan safe zones
- Add aspect-ratio adaptation
- Add a global UI scale setting
- Add per-widget scaling overrides
- Add crisp rendering across scales
- Add multi-display support
- Add orientation change handling
- Add scaling debug visualization

## Font Assets & Loading

- Add import of vector fonts
- Add font families with weights and styles
- Add signed-distance-field font generation
- Add multi-channel signed-distance-field fonts
- Add bitmap and pixel fonts
- Add a dynamic glyph atlas
- Add on-demand glyph rasterization
- Add font fallback chains for missing glyphs
- Add script-specific fallback fonts
- Add embedded and packaged fonts
- Add variable-font axis support
- Add font hinting and gamma settings
- Add font asset validation
- Add font memory budgets and eviction

## Text Layout & Shaping

- Add glyph shaping with kerning
- Add ligatures and contextual forms
- Add bidirectional text ordering
- Add complex-script shaping (Arabic, Indic, Thai)
- Add line breaking with language rules
- Add word wrap and character wrap
- Add justification and alignment
- Add tab stops and indentation
- Add letter and line spacing
- Add vertical text layout
- Add overflow handling (clip, ellipsis, scroll)
- Add auto-sizing and shrink-to-fit text
- Add mixed-font and mixed-size runs
- Add text measurement and metrics queries
- Add hit-testing from a point to a character
- Add caret and selection geometry from indices
- Add hyphenation
- Add layout caching for unchanged text

## Text Rendering & Effects

- Add glyph-atlas caching and packing
- Add signed-distance-field text rendering
- Add subpixel and grayscale antialiasing
- Add outlines and borders on text
- Add drop shadows and glow
- Add gradient and texture fill on text
- Add per-character color and opacity
- Add underline, strikethrough, and overline
- Add scaling without re-rasterization via distance fields
- Add crisp small-text rendering
- Add emoji and color-glyph rendering
- Add text material and shader customization
- Add text rendering budgets
- Add text rendering debug visualization

## Rich Text & Markup

- Add a rich-text markup language
- Add inline style spans (bold, italic, color, size)
- Add inline images and icons
- Add hyperlinks with click handling
- Add custom tags and decorators
- Add named text styles referenced in markup
- Add inline widgets embedded in text
- Add a typewriter and reveal effect
- Add animated and wavy text effects
- Add localized rich text with placeholders
- Add auto-linking of URLs and references
- Add markup validation and error reporting
- Add a rich-text editor preview
- Add sanitization of untrusted markup

## Text Input & Editing

- Add a caret with blinking and positioning
- Add text selection with mouse, touch, and keyboard
- Add keyboard navigation (word, line, document)
- Add input-method (IME) composition support
- Add clipboard cut, copy, and paste
- Add undo and redo of edits
- Add multi-line editing with scrolling
- Add auto-complete and suggestions
- Add input validation and masking
- Add character and length limits
- Add password masking with reveal
- Add placeholder and hint text
- Add on-screen and platform virtual keyboards
- Add rich-text editing controls
- Add spell-check hooks
- Add deterministic editing for tests

## Localization — String Tables & Keys

- Add localized string tables with keys
- Add namespaces and grouping of keys
- Add source-string gathering from content and code
- Add localized text references usable in widgets
- Add default and source-culture fallback text
- Add missing-translation detection and reporting
- Add key-collision detection
- Add context and comments for translators
- Add in-context localization editing
- Add stable keys that survive source-text changes
- Add per-key metadata (max length, do-not-translate)
- Add string-table import and merge
- Add string-table validation
- Add a localization dashboard with coverage

## Localization — Translation Pipeline

- Add extraction and gathering of translatable text
- Add export to standard translation formats
- Add import of translated files
- Add a translation-memory store
- Add reuse of prior translations for matches
- Add machine-translation hooks for drafts
- Add a review and approval workflow
- Add batch and incremental gathering
- Add per-target and per-platform packaging
- Add pluralization data in the pipeline
- Add screenshots and context for translators
- Add translation progress and status reporting
- Add conflict resolution on re-import
- Add pipeline validation and diagnostics

## Localization — Formatting & Culture

- Add culture and locale detection at startup
- Add explicit culture selection and switching
- Add culture fallback chains
- Add number formatting per culture
- Add date and time formatting per culture
- Add currency and percent formatting
- Add plural-rule selection per culture
- Add gendered and inflected text handling
- Add a message-format with named placeholders
- Add ordinal and cardinal formatting
- Add unit and measurement-system formatting
- Add list and conjunction formatting
- Add time-zone-aware formatting
- Add relative-time formatting (e.g. "3 minutes ago")
- Add culture-aware sorting and collation
- Add formatting validation and tests

## Localization — Assets, Fonts & RTL

- Add localized asset variants (textures, audio, video)
- Add per-culture asset selection at runtime
- Add per-culture and per-script fonts
- Add automatic font fallback per language
- Add right-to-left layout mirroring
- Add mirroring of icons and directional elements
- Add bidirectional mixed-content handling
- Add text-expansion handling for longer translations
- Add culture-specific styling overrides
- Add localized input and keyboard layouts
- Add pseudo-localization for testing
- Add validation of RTL and expansion issues
- Add per-culture number-input parsing
- Add localized-asset packaging

## Runtime Language Switching

- Add live language switching without a restart
- Add re-layout of UI on language change
- Add reload of strings, assets, and fonts on switch
- Add re-formatting of dynamic values on switch
- Add persistence of the chosen language
- Add a language-selection UI
- Add editor preview of any culture
- Add change notifications to widgets and gameplay
- Add graceful handling of partial translations
- Add language-switch performance budgets

## Accessibility — Screen Reader & Semantics

- Add an accessible widget tree parallel to the visual tree
- Add roles for widgets (button, checkbox, list, heading, etc.)
- Add accessible labels and descriptions
- Add live regions for announcements
- Add focus and selection reporting to assistive tech
- Add a defined reading order
- Add platform screen-reader integration
- Add custom-semantics overrides per widget
- Add state reporting (checked, expanded, disabled, busy)
- Add value and range reporting for sliders and progress
- Add grouping and landmark semantics
- Add localized accessibility text
- Add hints for available actions
- Add automatic label inference from content
- Add screen-reader testing hooks
- Add semantics debug inspection

## Accessibility — Visual & Motor

- Add text-scaling that reflows layout
- Add a global UI-scale accessibility option
- Add high-contrast modes
- Add colorblind-friendly palettes and filters
- Add reduced-motion mode
- Add strong focus indicators
- Add enlarged hit targets
- Add remappable UI navigation
- Add hold-to-toggle and sticky-input options
- Add dwell and one-switch scanning selection
- Add adjustable input timing and repeat
- Add cursor-size and pointer options
- Add pause and slow-down assists
- Add auto-advance and skip options for text
- Add flashing and photosensitivity safeguards
- Add motor-accessibility presets

## Accessibility — Audio & Feedback

- Add audio cues for UI focus and actions
- Add spoken descriptions of on-screen content
- Add haptic feedback for accessibility events
- Add customizable subtitle and caption styling
- Add a mono-audio option
- Add visual indicators for important sounds
- Add caption support for UI sounds
- Add volume-independent accessibility cues
- Add feedback-intensity settings
- Add validation of audio-accessibility coverage

## Accessibility — Settings & Compliance

- Add an accessibility settings menu
- Add accessibility presets and profiles
- Add first-run accessibility setup
- Add per-platform accessibility-API integration
- Add a compliance checklist against common guidelines
- Add an in-editor accessibility auditor
- Add contrast and text-size checks in the designer
- Add reporting of accessibility issues
- Add persistence and sync of accessibility settings
- Add documentation of accessibility features

## UI Performance & Batching

- Add draw-call batching across widgets
- Add invalidation panels to limit redraw
- Add retained-mode caching of static content
- Add widget pooling and recycling
- Add list and grid virtualization
- Add async and incremental widget construction
- Add layout caching for unchanged sub-trees
- Add per-frame UI time budgets
- Add texture-atlas usage for UI assets
- Add profiling and cost attribution per widget
- Add memory budgets for UI and fonts
- Add over-budget diagnostics

## UI Testing & Validation

- Add widget unit tests
- Add layout regression tests
- Add golden-image UI tests across resolutions
- Add input and navigation tests
- Add focus-order and tab-order tests
- Add data-binding correctness tests
- Add text-shaping and layout tests
- Add localization coverage and expansion tests
- Add right-to-left layout tests
- Add culture-formatting tests
- Add accessibility-semantics audits
- Add screen-reader announcement tests
- Add DPI and scaling tests
- Add UI performance stress tests

# Capability 17 · Networking / Multiplayer (Rust core)

**Objective:** Deliver a secure, versioned multiplayer stack with a stable Rust-to-C++ boundary, transport and session management, replication, prediction, rollback, authority, hosting, platform integration, simulation tools, diagnostics, and automated network testing.

## Rust Networking Core & FFI Boundary

- Add a Rust networking crate as the standalone network core
- Add a stable C-ABI boundary between the Rust core and the C++ engine
- Add a build integration that compiles the Rust crate into the engine
- Add typed opaque handles for connections, channels, and sessions across the boundary
- Add clear ownership and lifetime rules for buffers crossing the boundary
- Add zero-copy buffer passing where possible
- Add panic isolation so a Rust panic never unwinds into C++
- Add an async runtime (Tokio) owned by the network core
- Add a command queue from the engine thread into the network core
- Add an event queue from the network core back to the engine
- Add thread-safe, lock-light interchange between engine and network threads
- Add ABI versioning and compatibility checks
- Add error codes and diagnostics across the boundary
- Add memory-tracker integration for Rust allocations
- Add a headless build of the core for dedicated servers
- Add a WebAssembly build of the core for browser clients
- Add logging bridged from Rust into engine diagnostics
- Add configuration passed from the engine to the core

## Transport Layer

- Add a QUIC transport as the primary protocol
- Add TLS encryption and authentication via QUIC
- Add multiple QUIC streams for independent channels
- Add QUIC datagram support for unreliable traffic
- Add congestion control and pacing
- Add path-MTU discovery
- Add segmentation-offload use where the platform supports it
- Add a reliable-UDP transport as an alternative
- Add a connection protocol with secure tokens and key exchange
- Add a WebTransport backend for browser clients
- Add a WebSocket fallback transport
- Add a platform-socket backend (console and store networking)
- Add NAT traversal and hole punching
- Add relay and fallback routing when direct connection fails
- Add per-transport capability negotiation
- Add transport selection and automatic fallback
- Add packet fragmentation and reassembly
- Add transport-level statistics (throughput, loss, RTT)

## Connection & Session Management

- Add connect and disconnect flows
- Add a handshake with version and capability exchange
- Add authentication tokens and validation
- Add session lifecycle and identifiers
- Add keepalive and timeout handling
- Add graceful and abrupt disconnect handling
- Add automatic reconnection with backoff
- Add connection-quality metrics (RTT, jitter, loss)
- Add per-connection state and user data
- Add connection limits and admission control
- Add ban and kick support
- Add session migration between transports
- Add connection events surfaced to gameplay
- Add connection debugging tools

## Serialization & Wire Format

- Add a compact binary wire format
- Add zero-copy serialization for hot paths
- Add bit-packing of small fields and flags
- Add quantization of positions, rotations, and scales
- Add delta encoding against a baseline
- Add schema and version tags for messages
- Add forward- and backward-compatible message evolution
- Add endian-independent encoding
- Add optional compression for large payloads
- Add string interning and dictionary compression
- Add a code-generated schema from component reflection
- Add per-field precision and range annotations
- Add validation and bounds checking on decode
- Add fuzzing of the decoder against malformed input
- Add serialization benchmarks
- Add wire-format debugging and inspection

## ECS Replication Integration

- Map engine entities to stable network identifiers
- Add a bidirectional mapping between network ids and flecs entities
- Add server and client world roles
- Drive replication from component-change tracking (modified flags and changed filters)
- Add component observers feeding spawn, update, and despawn events
- Enumerate replicated fields generically through component reflection
- Add per-component replication opt-in and configuration
- Apply remote changes through the deferred command buffer
- Add deterministic entity-id remapping on the client
- Reuse the chunked world-snapshot codec for baselines
- Reuse delta snapshots for per-tick updates
- Add parent and reference remapping across the network
- Add replication of relationships, tags, and singletons
- Add batched, chunk-friendly replication reads
- Add SIMD-friendly packing of replicated component chunks
- Add a replication schedule within the system scheduler
- Add replication of structural changes (add/remove component)
- Add server-side authoritative apply and validation
- Add a replication registry mapping component types to codecs
- Add replication configuration as a data asset

## Entity & Component Replication

- Add server-authoritative entity spawn replication
- Add entity despawn replication
- Add per-component state replication
- Add replication groups and priorities
- Add replicate-on-change versus replicate-every-tick modes
- Add initial-state replication on join
- Add late-join catch-up replication
- Add replication of spawned prefabs and templates
- Add ownership tags carried with entities
- Add relevancy filtering per entity and client
- Add replication of transform with quantization and smoothing
- Add replication of animation and gameplay state
- Add replication frequency per component
- Add replication of arrays and dynamic buffers
- Add conditional replication by role and authority
- Add replication of destruction and pooled entities
- Add replication conflict resolution
- Add replication debugging per entity

## Snapshots & Delta Compression

- Add periodic world snapshots
- Add per-entity and per-component deltas against a baseline
- Add acknowledgement of received snapshots
- Add baseline management from acked snapshots
- Add a snapshot ring buffer of recent states
- Add delta encoding of only changed fields
- Add run-length and bit-mask encoding of change sets
- Add priority-based partial snapshots under bandwidth limits
- Add reliable baseline recovery after loss
- Add compression of snapshot payloads
- Add snapshot size budgets and diagnostics
- Add snapshot history for lag compensation and rollback
- Add deterministic snapshot ordering
- Add snapshot debugging and inspection

## Reliability Channels & Messaging

- Add a reliable-ordered channel
- Add a reliable-unordered channel
- Add an unreliable channel
- Add an unreliable-sequenced channel
- Add per-channel configuration and priority
- Add message fragmentation for large messages
- Add message batching and coalescing
- Add acknowledgement and retransmission for reliable channels
- Add ordering guarantees per channel
- Add flow control per channel
- Add channel statistics and diagnostics
- Add a typed message registry
- Add message priority and starvation avoidance
- Add channel debugging tools

## RPC & Events

- Add client-to-server remote procedure calls
- Add server-to-client remote procedure calls
- Add multicast remote procedure calls
- Add typed RPC parameters from reflection
- Add per-RPC reliability and channel selection
- Add request and response RPCs with correlation
- Add validation and rate limiting of incoming RPCs
- Add ordering guarantees relative to replication
- Add a scripting API for RPCs and network events
- Add deterministic RPC handling for replay
- Add RPC diagnostics and logging
- Add RPC schema versioning

## Input, Ticks & Clock Sync

- Add a fixed network tick decoupled from frame rate
- Add tick-buffered input channels
- Add delivery of input at the matching server tick
- Add input prediction and buffering
- Add clock synchronization between client and server
- Add round-trip and one-way delay estimation
- Add time-offset smoothing and drift correction
- Add tick adjustment to keep clients aligned
- Add input redundancy against packet loss
- Add input acknowledgement and resend
- Add server-side input validation and clamping
- Add deterministic input application order
- Add input-timeline debugging
- Add tick and clock diagnostics

## Client-Side Prediction & Reconciliation

- Add prediction of the locally controlled entity
- Add application of local input immediately
- Add storage of predicted state per tick
- Add reconciliation against authoritative snapshots
- Add rollback of predicted state on mismatch
- Add re-simulation from the corrected state
- Add prediction of owned non-player entities
- Add predicted spawning with server confirmation
- Add error smoothing to hide small corrections
- Add snap thresholds for large corrections
- Add prediction masks per component
- Add prediction of physics-driven entities
- Add reconciliation diagnostics and error metrics
- Add prediction debugging visualization
- Add deterministic prediction for tests
- Add integration with the command buffer for predicted changes

## Rollback Netcode

- Add a rollback simulation model for deterministic games
- Add input delay configuration
- Add confirmed-frame tracking
- Add save and restore of simulation state per frame
- Reuse world snapshots for fast state save and restore
- Add re-simulation on receiving remote input
- Add prediction of remote input until confirmed
- Add a rollback window and frame budget
- Add peer-to-peer lockstep synchronization
- Add a desync detection and sync-test mode
- Add checksum comparison across peers
- Add catch-up and frame-skipping under load
- Add deterministic math and iteration ordering hooks
- Add rollback of audio and visual effects
- Add rollback diagnostics and replay
- Add rollback determinism tests

## Snapshot Interpolation

- Add interpolation of remotely replicated entities
- Add an interpolation delay and buffer
- Add a snapshot buffer of recent remote states
- Add hermite and linear interpolation of transforms
- Add extrapolation when snapshots are late
- Add clamping and blending of extrapolation error
- Add per-entity interpolation configuration
- Add interpolation of custom replicated fields
- Add smoothing of teleports and discontinuities
- Add interpolation for animation and gameplay state
- Add interpolation diagnostics
- Add interpolation debugging visualization

## Authority & Ownership

- Add server authority over entities by default
- Add client authority for owned entities
- Add per-component authority configuration
- Add authority transfer between server and clients
- Add ownership assignment on spawn
- Add authority-aware replication and prediction
- Add conflict resolution when authority changes
- Add validation of client-authored state on the server
- Add authority events surfaced to gameplay
- Add authority debugging and inspection
- Add ownership persistence across reconnection
- Add authority tests

## Interest Management & Relevancy

- Add area-of-interest per client
- Add spatial culling of replication by distance
- Add relevancy queries integrated with the spatial structure
- Add priority scoring per entity and client
- Add streaming-integrated relevancy for large worlds
- Add team and faction relevancy rules
- Add always-relevant and never-relevant tagging
- Add relevancy hysteresis to avoid churn
- Add per-client relevancy budgets
- Add relevancy updates spread across ticks
- Add relevancy debugging visualization
- Add relevancy performance budgets
- Add scalable relevancy for many clients
- Add relevancy tests

## Bandwidth & Rate Control

- Add a per-client bandwidth budget
- Add a priority accumulator for fair replication
- Add rate limiting and shaping of outgoing traffic
- Add congestion-aware send scheduling
- Add adaptive quality under constrained bandwidth
- Add per-channel bandwidth allocation
- Add drop and defer policies under pressure
- Add measurement of available bandwidth
- Add compression trade-off tuning
- Add bandwidth statistics per client and channel
- Add over-budget diagnostics
- Add bandwidth stress tests

## Lag Compensation

- Add a historical state buffer for entities
- Add server rewind to a client's perceived time
- Add rewound hit detection for shooting
- Add per-client latency estimation for rewind
- Add rewound queries integrated with physics
- Add configurable rewind window limits
- Add favor-the-shooter and favor-the-target policies
- Add interpolation-aware rewind
- Add anti-abuse limits on rewind
- Add lag-compensation debugging visualization
- Add lag-compensation diagnostics
- Add lag-compensation tests

## Networked Physics

- Add replication of physics body state
- Add client prediction of physics
- Add server reconciliation of physics
- Add deterministic physics stepping for networked sim
- Add rollback and re-simulation of physics
- Add authority handling for physics objects
- Add sleeping and relevancy for networked bodies
- Add compressed physics-state encoding
- Add reconciliation smoothing for physics
- Add networked constraint and joint state
- Add networked destruction and break events
- Add networked-physics tests

## Dedicated Servers & Hosting

- Add a headless dedicated-server mode
- Add server startup, configuration, and shutdown
- Add multiple game instances per process
- Add sharding and world instancing across servers
- Add server discovery and registration
- Add load balancing across server instances
- Add container and orchestration friendliness
- Add horizontal scaling and autoscaling hooks
- Add server health checks and heartbeats
- Add graceful drain and instance handoff
- Add server-side logging and metrics export
- Add hot-config and remote administration
- Add crash recovery and instance restart
- Add server-side determinism for authoritative sim
- Add resource budgets per instance
- Add server-side anti-cheat validation hooks

## Listen Server, P2P & Host Migration

- Add a listen-server mode where the host is a player
- Add peer-to-peer connectivity
- Add host election and migration on host loss
- Add state transfer during host migration
- Add relay fallback when direct P2P fails
- Add mesh and star topologies
- Add authority handling in P2P sessions
- Add seamless client experience through migration
- Add P2P security and validation
- Add P2P and migration tests

## Matchmaking, Lobbies & Sessions

- Add lobby creation and joining
- Add rooms with capacity and settings
- Add ready-up and start flows
- Add party and group support
- Add skill-based matchmaking
- Add region and latency-based matching
- Add queue management and estimated wait
- Add backfill for in-progress matches
- Add dedicated-server allocation on match found
- Add reconnection to an active match
- Add invites and join-by-code
- Add presence and friends integration hooks
- Add session metadata and browsing
- Add matchmaking rules configuration
- Add matchmaking diagnostics
- Add matchmaking tests

## Security & Anti-Cheat

- Add encryption and authentication on all traffic
- Add server-side validation of all client inputs
- Add authority checks so clients cannot mutate others
- Add replay-attack protection with nonces and sequence checks
- Add rate limiting and flood protection
- Add packet sanity and bounds validation
- Add movement and physics sanity checks
- Add statistical cheat-detection hooks
- Add integrity checks and tamper detection hooks
- Add denial-of-service mitigation at the transport
- Add connection throttling and blacklisting
- Add secure token issuance and rotation
- Add audit logging of suspicious activity
- Add sandboxing of untrusted message handling
- Add privacy handling of player data
- Add security tests and fuzzing

## Web & Cross-Platform Transport

- Add a browser client over WebTransport
- Add a browser fallback over WebSocket
- Add a peer-to-peer browser path over web real-time transport
- Add a shared code path across native and web builds
- Add console and store platform networking backends
- Add platform certificate and trust handling
- Add cross-play between platforms
- Add platform-specific NAT and relay handling
- Add capability differences handled per platform
- Add cross-platform networking tests

## Voice & Media over Network

- Add a transport path for encoded voice
- Add positional voice synchronized with entities
- Add voice channels tied to sessions and teams
- Add jitter buffering for networked voice
- Add bandwidth sharing between voice and gameplay
- Add mute, block, and priority for voice
- Add optional media and data-stream transport
- Add voice-network diagnostics
- Add voice-network security
- Add voice-network tests

## Network Simulation, Debugging & Metrics

- Add a network simulator injecting latency, jitter, and loss
- Add reorder and duplication injection
- Add bandwidth throttling in the simulator
- Add a packet inspector and logger
- Add a replication visualizer per entity and client
- Add per-client bandwidth and RTT dashboards
- Add recording and replay of network sessions
- Add deterministic replay from recorded traffic
- Add a desync inspector for rollback and prediction
- Add server and client profiling of network cost
- Add machine-readable network metrics for CI
- Add live network overlays in play
- Add breadcrumb logging for connection issues
- Add reproduction capture for bug reports
- Add alerting hooks for server health
- Add simulation presets (mobile, wifi, congested)

## Testing & Validation

- Add transport reliability and ordering tests
- Add serialization round-trip and fuzz tests
- Add replication correctness tests
- Add entity-id mapping and remapping tests
- Add prediction and reconciliation tests
- Add rollback determinism and sync tests
- Add snapshot and delta correctness tests
- Add interest-management correctness tests
- Add bandwidth and rate-control tests
- Add lag-compensation tests
- Add networked-physics tests
- Add matchmaking and lobby tests
- Add security and anti-cheat tests
- Add cross-platform and web-client tests
- Add latency, loss, and reorder soak tests
- Add many-client scalability stress tests
- Add host-migration and reconnection tests
- Add FFI-boundary safety and leak tests

# Capability 18 · AI / Navigation / Behavior / Perception / Crowd

**Objective:** Deliver scalable navigation, decision-making, perception, tactical reasoning, interaction, crowd behavior, authoring, debugging, and gameplay integration for individual agents and large simulated populations.

## Navigation Mesh & Generation

- Add a navigation mesh (navmesh): a polygon mesh of walkable surfaces generated from world geometry
- Add voxel rasterization of collision geometry for navmesh building
- Add walkable-surface filtering by slope and height
- Add region and contour generation from voxels
- Add convex-polygon and detail-mesh generation
- Add a tiled navmesh for large worlds
- Add per-tile build and streaming with the world
- Add navmesh generation from terrain and heightfields
- Add multiple navmeshes for different agent sizes
- Add agent radius, height, step, and slope parameters
- Add area types baked into the navmesh
- Add navigation-bounds volumes defining where to build
- Add exclude and include volumes
- Add offline baking of static navmesh
- Add runtime generation for dynamic worlds
- Add incremental rebuild of changed tiles
- Add navmesh serialization and streaming
- Add navmesh validation and gap detection
- Add a one-click bake with sensible defaults
- Add navmesh generation diagnostics

## Navigation Server & Runtime

- Add a central navigation service that owns navmeshes and queries
- Add path requests routed through the service
- Add synchronous and asynchronous path queries
- Add a query for the nearest point on the navmesh
- Add reachability and connectivity queries
- Add ray and walkability checks along the navmesh
- Add multiple navigation maps (2D and 3D)
- Add per-map configuration and layers
- Add thread-safe query submission from gameplay
- Add batched path queries across the worker pool
- Add a query cache for repeated requests
- Add navigation events surfaced to gameplay
- Add a scripting API for navigation queries
- Add navigation-service diagnostics

## Pathfinding Algorithms

- Add an A* pathfinder over the navmesh
- Add configurable heuristics and cost functions
- Add hierarchical pathfinding for long paths
- Add path smoothing with the funnel (string-pulling) algorithm
- Add any-angle pathfinding for open areas
- Add partial paths when the goal is unreachable
- Add path corridors that bound local steering
- Add path re-planning on navmesh change
- Add cost overrides and penalties per area
- Add waypoint and graph-based navigation as an option
- Add multi-goal and nearest-of-many queries
- Add flow-field pathfinding for many agents to one goal
- Add path budgets and time-sliced search
- Add deterministic pathfinding for replay and tests
- Add path caching and reuse
- Add pathfinding diagnostics

## Navigation Agents

- Add a navigation-agent component that moves along paths
- Add a move-to-location command
- Add a move-to-and-follow-actor command
- Add agent radius, height, and shape
- Add speed, acceleration, and turning limits
- Add stopping distance and arrival handling
- Add path following with corner cutting
- Add automatic re-path on blockage
- Add binding of an agent to a specific navmesh
- Add ground snapping and off-navmesh recovery
- Add pause, resume, and stop of movement
- Add velocity output consumed by movement and animation
- Add root-motion and physics-movement reconciliation
- Add agent state and events (moving, blocked, arrived)
- Add a scripting API for agent movement
- Add agent debug visualization

## Dynamic Navigation & Obstacles

- Add dynamic obstacles that carve the navmesh
- Add navigation modifiers that change area cost at runtime
- Add moving-obstacle handling with local avoidance
- Add door and gate navigation state
- Add dirty-region tracking for partial rebuilds
- Add async rebuild of affected navmesh tiles
- Add temporary blocking and unblocking of areas
- Add obstacle shapes (box, cylinder, convex)
- Add priority between carving and cost modifiers
- Add rebuild budgets to avoid frame spikes
- Add invalidation events to re-path affected agents
- Add dynamic-navigation diagnostics
- Add dynamic-navigation debug visualization
- Add validation of navmesh consistency after edits

## Off-Mesh Links & Traversal

- Add off-mesh links connecting disconnected navmesh areas
- Add jump and drop-down links
- Add ladder and climb links
- Add teleport and portal links
- Add automatic link generation from geometry
- Add hand-placed links in the editor
- Add smart links that trigger traversal animations
- Add per-link cost and agent filtering
- Add one-way and bidirectional links
- Add link entry and exit alignment
- Add link traversal events for gameplay and animation
- Add off-mesh-link debug visualization

## Local Avoidance & Steering

- Add reciprocal velocity-obstacle (RVO/ORCA) avoidance between agents
- Add a seek steering behavior
- Add flee and evade behaviors
- Add arrive with deceleration
- Add pursue and intercept behaviors
- Add wander and patrol behaviors
- Add separation, cohesion, and alignment (flocking)
- Add obstacle-avoidance steering
- Add wall-following and corridor-following
- Add priority and weighting between behaviors
- Add avoidance priority so important agents pass first
- Add crowd-aware avoidance quality levels
- Add avoidance of dynamic non-agent obstacles
- Add smoothing to avoid jitter and deadlocks
- Add deadlock detection and resolution
- Add avoidance layers and masks
- Add steering integration with animation and physics
- Add avoidance debug visualization

## Navigation Areas, Costs & Filters

- Add named navigation area types (default, water, road, danger)
- Add per-area traversal cost
- Add painting of areas onto the navmesh
- Add area assignment from surface materials and volumes
- Add query filters selecting allowed areas per agent
- Add per-agent cost multipliers
- Add temporary cost overrides (avoid fire, prefer cover)
- Add area-based include and exclude rules
- Add flags for jump-required and swim areas
- Add filter presets for common agent types
- Add area and cost debug visualization
- Add validation of area configuration

## Behavior Trees

- Add a behavior-tree asset and runtime
- Add sequence and selector composite nodes
- Add parallel composite nodes
- Add a random and weighted selector
- Add decorator nodes for conditions
- Add loop, cooldown, and time-limit decorators
- Add blackboard-based condition decorators
- Add task (leaf) action nodes
- Add latent tasks that run over multiple frames
- Add service nodes that tick while a branch is active
- Add lower-priority and self aborts
- Add observer-driven aborts from blackboard changes
- Add subtrees and reusable behavior modules
- Add dynamic subtree injection
- Add a task and node library
- Add custom task and decorator authoring
- Add per-node instance data
- Add tree evaluation off the main thread
- Add deterministic tree evaluation for tests
- Add tree hot-reload
- Add behavior-tree debugging (active path, node states)
- Add behavior-tree authoring in a visual editor

## Blackboard

- Add a typed blackboard of key-value data
- Add scalar, vector, entity, and enum key types
- Add per-agent blackboard instances
- Add a shared blackboard for groups
- Add key observers that notify on change
- Add default values and key metadata
- Add writing from perception and gameplay
- Add reading from behavior, utility, and planners
- Add blackboard serialization for save and network
- Add blackboard debugging and live inspection
- Add blackboard validation
- Add a scripting API for blackboard access

## Utility AI

- Add a utility-based decision system
- Add actions with scored considerations
- Add response curves mapping inputs to scores
- Add weighting and combination of considerations
- Add context and target scoring
- Add highest-score and weighted-random selection
- Add cooldowns and inertia to avoid thrashing
- Add data-driven utility definitions
- Add integration with behavior trees as a selector
- Add per-agent utility tuning
- Add authoring of considerations and curves
- Add utility scoring debug visualization
- Add deterministic utility evaluation
- Add utility validation

## Goal-Oriented Action Planning

- Add goals with desired world-state conditions
- Add actions with preconditions and effects
- Add a symbolic world-state representation
- Add a planner that searches action space with A*
- Add action costs and plan optimization
- Add dynamic replanning on world-state change
- Add goal selection and prioritization
- Add plan execution with per-action monitoring
- Add plan invalidation and recovery
- Add sensors feeding the world state
- Add data-driven goals and actions
- Add planning budgets and time-slicing
- Add plan debugging and visualization
- Add deterministic planning for tests

## Hierarchical Task Networks

- Add primitive and compound tasks
- Add methods that decompose compound tasks
- Add preconditions on methods and tasks
- Add a planner that builds a task hierarchy
- Add partial and re-planning support
- Add world-state integration with sensors
- Add domain authoring for task networks
- Add plan execution and monitoring
- Add planning budgets
- Add planning diagnostics and visualization

## AI State Machines

- Add a reusable AI finite state machine
- Add states with enter, update, and exit
- Add condition- and event-driven transitions
- Add a state stack for interruptions
- Add hierarchical AI states
- Add blackboard-driven transitions
- Add integration with behavior trees and utility
- Add state serialization for save and network
- Add state debugging and visualization
- Add deterministic state evaluation

## Perception System

- Add a perception component with configurable senses
- Add a sight sense with range, cone, and line-of-sight
- Add peripheral vision and central-focus falloff
- Add a hearing sense with radius and loudness
- Add a touch and collision sense
- Add a damage sense reacting to being hit
- Add a team and affiliation sense
- Add stimuli sources that emit sight, sound, and events
- Add line-of-sight checks against geometry
- Add detection accumulation over time (awareness meter)
- Add detection thresholds and states (unaware, suspicious, alert)
- Add stealth interaction (light, cover, noise, crouch)
- Add perception updates budgeted and time-sliced
- Add async line-of-sight queries
- Add perception events routed to behavior and blackboard
- Add a scripting API for perception
- Add perception debug visualization (cones, hearing radius)
- Add perception validation

## Sensory Memory & Awareness

- Add memory of perceived targets
- Add last-known-position tracking
- Add forgetting over time
- Add confidence and staleness of memories
- Add threat assessment and target selection
- Add group and shared perception memory
- Add investigation of last-known positions
- Add search behavior when a target is lost
- Add alertness propagation between agents
- Add reaction times and detection delays
- Add memory serialization for save and network
- Add awareness-state events for gameplay and UI
- Add memory debugging and inspection
- Add memory validation

## Environment Queries & Spatial Reasoning

- Add a spatial query system that scores locations and actors
- Add grid, ring, and points-around generators
- Add path-and-actor-based generators
- Add distance and dot-product tests
- Add line-of-sight and trace tests
- Add overlap and clearance tests
- Add navmesh-reachability tests
- Add scoring, weighting, and normalization of results
- Add best-single and best-N run modes
- Add async query execution and time-slicing
- Add query use inside behavior trees and utility
- Add data-driven query definitions
- Add caching of query results
- Add query authoring tools
- Add query debug visualization (scored points)
- Add deterministic query evaluation

## Influence Maps & Tactical Reasoning

- Add influence maps of threat, presence, and control
- Add propagation and decay of influence
- Add multiple layers (danger, allies, objectives)
- Add sampling of maps for decision inputs
- Add a cover system with cover points and quality
- Add tactical position selection (flank, retreat, advance)
- Add interaction points and usable objects for AI
- Add occupancy and reservation of positions and objects
- Add group tactical coordination
- Add influence-map budgets and resolution control
- Add influence and cover debug visualization
- Add tactical-reasoning authoring
- Add deterministic tactical evaluation
- Add tactical-reasoning validation

## Crowd Simulation

- Add a crowd manager coordinating many agents
- Add shared local avoidance across the crowd
- Add crowd flow and lane formation
- Add density-aware movement and slowdown
- Add goal and destination assignment for crowds
- Add spawning and despawning of crowd agents
- Add crowd navigation on the shared navmesh
- Add priority and politeness rules
- Add congestion and bottleneck handling
- Add ambient crowd behaviors (idle, wander, react)
- Add crowd reaction to events and hazards
- Add crowd LOD by distance and visibility
- Add crowd budgets and quality scaling
- Add crowd debug visualization
- Add crowd authoring and presets
- Add crowd validation

## Large-Scale / Mass AI

- Add a data-oriented agent representation in chunk storage
- Add processing of mass agents with SIMD kernels
- Add parallel agent updates across the worker pool
- Add behavior LOD (full behavior near, simplified far)
- Add update-rate LOD by distance and visibility
- Add representation LOD (full agent, proxy, static)
- Add flow-field navigation for huge agent counts
- Add shared and pooled behavior state
- Add spatial partitioning of mass agents
- Add promotion and demotion between detailed and mass agents
- Add bulk spawn and despawn through the command buffer
- Add zero-copy handoff of agent transforms to rendering
- Add memory-traffic-aware batch sizes
- Add deterministic parallel agent updates
- Add scaling to hundreds of thousands of agents within budget
- Add mass-AI throughput diagnostics
- Add a mass-AI stress harness
- Add mass-AI debug visualization

## Formations & Group Behavior

- Add formation definitions (line, column, wedge, circle)
- Add slot assignment within a formation
- Add leader-follower movement
- Add formation maintenance while moving and turning
- Add dynamic re-forming after obstacles
- Add group goals and coordinated tasks
- Add role assignment within a group
- Add squad-level decision making
- Add communication and shared blackboard for groups
- Add formation authoring and presets
- Add formation debug visualization
- Add formation validation

## AI Agent & Brain Components

- Add an AI brain component that drives an agent
- Add composition of navigation, perception, and behavior components
- Add agent lifecycle (spawn, activate, deactivate, destroy)
- Add per-agent configuration assets
- Add agent templates and archetypes
- Add agent teams, factions, and relationships
- Add agent difficulty and skill parameters
- Add agent state persistence for save and network
- Add agent messaging and events
- Add agent enable and disable by relevancy
- Add a scripting API for agent control
- Add agent debugging and inspection
- Add reusable agent presets
- Add agent validation

## AI Authoring & Editor Tools

- Add a behavior-tree visual editor
- Add a utility and consideration editor
- Add a planner domain editor for goals and actions
- Add a blackboard editor
- Add an environment-query editor
- Add navmesh build settings and preview in the editor
- Add area painting and link placement tools
- Add perception setup and preview
- Add crowd and formation authoring
- Add agent template and preset authoring
- Add live preview of AI in play
- Add copy, paste, and reuse of AI assets
- Add undo and redo across AI edits
- Add templates and starting AI setups
- Add a gallery of example AI to learn from
- Add AI-asset validation and warnings

## AI Scripting & Gameplay Integration

- Add a scripting API for custom tasks and decorators
- Add custom considerations and planner actions from script
- Add AI events consumable by gameplay
- Add gameplay commands to AI (move, attack, follow, flee)
- Add integration with the gameplay component library
- Add hooks into animation for AI-driven motion
- Add integration with dialogue and interaction systems
- Add data-driven AI configuration
- Add deterministic AI for replay and tests
- Add AI-scripting debugging
- Add hot-reload of AI scripts
- Add AI-scripting validation

## AI Debugging & Visualization

- Add a gameplay debugger overlay for selected agents
- Add navmesh and path visualization
- Add perception visualization (sight cones, hearing, stimuli)
- Add behavior-tree active-path and node-state display
- Add blackboard live values
- Add planner and plan visualization
- Add utility-score breakdown display
- Add environment-query scored-point display
- Add influence-map and cover visualization
- Add avoidance and steering vectors
- Add agent state and memory inspection
- Add per-agent AI timing and cost
- Add recording and replay of AI decisions
- Add filtering and selection of debugged agents
- Add a screenshot-friendly clean AI overlay
- Add step-through of AI decisions

## AI Performance & Scaling

- Add time-slicing of AI updates across frames
- Add per-agent and per-system update budgets
- Add distance- and visibility-based AI LOD
- Add parallel AI evaluation across the worker pool
- Add job-graph scheduling of perception, planning, and navigation
- Add pooling of AI state and query buffers
- Add caching of paths, queries, and line-of-sight
- Add async navigation and query execution
- Add dormancy for off-screen and distant agents
- Add memory budgets for AI data
- Add profiling and cost attribution per AI subsystem
- Add a headless AI benchmark harness
- Add machine-readable AI metrics for CI
- Add over-budget diagnostics with responsible agents

## AI User-Friendly Setup

- Add a one-click navmesh bake for a level
- Add automatic agent setup from a character
- Add ready-made behavior presets (patrol, guard, chase, flee, wander)
- Add drag-and-drop AI behaviors onto agents
- Add plain-language behavior and perception settings
- Add sensible defaults so an agent walks and reacts immediately
- Add a beginner mode that hides advanced tuning
- Add guided setup for navigation, perception, and behavior
- Add live preview and one-click test of AI
- Add friendly warnings with one-click fixes
- Add a gallery of example agents to open and tweak
- Add consistent, reversible AI authoring

## AI Testing & Validation

- Add navmesh generation correctness tests
- Add pathfinding correctness and determinism tests
- Add path-smoothing and corridor tests
- Add local-avoidance and deadlock tests
- Add off-mesh-link traversal tests
- Add behavior-tree execution and abort tests
- Add utility and planner determinism tests
- Add perception detection and line-of-sight tests
- Add memory and awareness-state tests
- Add environment-query correctness tests
- Add crowd flow and congestion tests
- Add mass-AI scale and performance stress tests
- Add formation-maintenance tests
- Add dynamic-navmesh rebuild tests
- Add save and network state tests for AI
- Add golden-scenario AI regression tests

## 3D & Volumetric Navigation

- Add volumetric navigation data for flying and swimming agents
- Add a sparse voxel octree for the 3D navigation space
- Add 3D pathfinding through open volumes
- Add navigation volumes that bound where 3D nav is built
- Add free-flight and tethered-flight movement
- Add swimming navigation within water bodies
- Add 3D local avoidance between flying agents
- Add height and ceiling constraints for 3D agents
- Add hybrid navigation switching between navmesh and volume
- Add links between ground and air navigation
- Add 3D path smoothing and corridors
- Add streaming and rebuild of volumetric nav data
- Add 3D navigation debug visualization
- Add 3D navigation tests

## Smart Objects & Environment Interaction

- Add smart objects that advertise available actions
- Add interaction slots with entry points and directions
- Add contextual animations bound to smart-object use
- Add querying of nearby smart objects by an agent
- Add filtering of objects by tags, needs, and conditions
- Add reservation and release of interaction slots
- Add multi-agent interactions on shared objects
- Add preconditions and gameplay effects on use
- Add navigation to and alignment with interaction slots
- Add interruption and abort of interactions
- Add smart-object authoring and setup
- Add runtime registration of smart objects
- Add smart-object debug visualization
- Add smart-object tests

## Combat & Tactical AI

- Add a combat behavior layer for engaging targets
- Add target selection and threat prioritization
- Add aiming with accuracy, spread, and skill
- Add weapon handling (fire, reload, switch)
- Add firing patterns (burst, suppressive, aimed)
- Add taking and using cover during combat
- Add peeking and blind-fire from cover
- Add flanking and advancing maneuvers
- Add retreat and disengage under pressure
- Add grenade and ability usage decisions
- Add reaction to incoming fire and suppression
- Add coordinated squad fire and movement
- Add engagement ranges and positioning
- Add difficulty tuning of combat skill
- Add combat-AI debug visualization
- Add combat-AI tests

## Vehicle & Traffic AI

- Add AI drivers that control vehicles
- Add path and racing-line following for vehicles
- Add speed control for corners and obstacles
- Add a road-network and lane graph for traffic
- Add lane following and lane changing
- Add intersection and right-of-way handling
- Add traffic rules (signals, signs, speed limits)
- Add obstacle and collision avoidance for vehicles
- Add pedestrian and cross-traffic yielding
- Add parking and pull-over behaviors
- Add traffic density and spawning management
- Add vehicle-AI difficulty and aggression
- Add vehicle and traffic debug visualization
- Add vehicle and traffic AI tests

## AI Director & Encounter Management

- Add an AI director that paces gameplay intensity
- Add intensity build-up and relaxation cycles
- Add dynamic difficulty adjustment
- Add encounter and spawn scheduling
- Add population budgets and caps
- Add spawn points and reinforcement waves
- Add relevancy-aware spawning around players
- Add despawn of irrelevant and distant agents
- Add encounter authoring and rules
- Add adaptive spawning from player performance
- Add director hooks for scripted moments
- Add director state persistence
- Add director debug visualization
- Add director tests

## NPC Schedules, Needs & Social AI

- Add daily schedules and routines for NPCs
- Add time-of-day-driven activity selection
- Add a needs model (hunger, rest, social, hygiene)
- Add utility-driven satisfaction of needs
- Add ownership of homes, jobs, and locations
- Add relationships and reputation between characters
- Add mood and emotion affecting behavior
- Add social interactions between NPCs
- Add knowledge and gossip spreading between agents
- Add reactions to player actions and world events
- Add ambient settlement-life behaviors
- Add schedule interruption and recovery
- Add data-driven routine and needs authoring
- Add schedule and needs persistence in saves
- Add social-AI debug visualization
- Add social-AI tests

## Dialogue, Barks & Conversation AI

- Add a bark system for contextual voice lines
- Add trigger conditions for barks (combat, idle, reaction)
- Add priority, cooldown, and deduplication of barks
- Add group and call-and-response barks
- Add conversation between two or more agents
- Add turn-taking and response selection
- Add interruption of conversation by events
- Add subtitle and localization hooks for lines
- Add lip-sync and facial-animation triggers
- Add data-driven line banks and rules
- Add bark and conversation debugging
- Add dialogue-AI tests

## Machine Learning & Learning Agents

- Add a learning-agent framework for trained behaviors
- Add sensor, action, and reward definitions
- Add reinforcement-learning training support
- Add imitation learning from recorded play
- Add a neural-network inference runtime
- Add on-device inference budgets and batching
- Add hybrid behaviors combining learned and scripted logic
- Add training-environment and episode management
- Add model versioning and hot-swap
- Add determinism and reproducibility controls
- Add safety and fallback when a model misbehaves
- Add learned navigation and locomotion policies
- Add learning-agent diagnostics
- Add learning-agent tests

## AI Motion & Locomotion Integration

- Add animation-driven movement for AI agents
- Add motion-matching selection for AI locomotion
- Add procedural locomotion for varied body types
- Add foot placement and IK on uneven ground
- Add turn-in-place and pivot handling
- Add speed and stride warping to match path speed
- Add contextual animation selection (walk, sneak, injured)
- Add smooth start, stop, and direction changes
- Add moving-platform and dynamic-surface handling
- Add root-motion-and-navigation reconciliation
- Add locomotion debug visualization
- Add locomotion-integration tests

## Reactions, Interrupts & Emotes

- Add a reaction system for immediate responses
- Add flinch, dodge, and stagger reactions
- Add take-cover reflexes under fire
- Add interruption of the current behavior by priority events
- Add resumption of interrupted behavior
- Add hit and damage reactions
- Add alert and startle reactions
- Add emotes and gestures for communication
- Add reaction cooldowns and blending
- Add reaction-system tests

# Capability 19 · Advanced Rendering & Ray Tracing

**Objective:** Extend the validated renderer foundation with capability-gated ray tracing, advanced shadows and shading, reconstruction, wide-gamut HDR output, robust fallbacks, diagnostics, and platform-specific performance budgets.

## Ray-Tracing Foundation

- Add ray-tracing acceleration structures (bottom-level and top-level)
- Add build, refit, and compaction of acceleration structures
- Add per-instance transforms and masks in the top-level structure
- Add a ray-tracing pipeline with ray-generation, hit, and miss shaders
- Add inline ray queries for lightweight tracing
- Add ray payloads and recursion controls
- Add alpha-tested and transparent hit handling
- Add streaming and updating of acceleration structures for dynamic scenes
- Add skinned-mesh acceleration-structure updates
- Add a capability check and raster fallback where ray tracing is unavailable
- Add ray-tracing cost budgets and diagnostics
- Add ray-tracing debug visualization

## Ray-Traced Lighting Effects

- Add ray-traced reflections
- Add ray-traced shadows with soft penumbra
- Add ray-traced ambient occlusion
- Add ray-traced global illumination
- Add ray-traced translucency and refraction
- Add hybrid ray-traced and screen-space composition
- Add ray count and quality scaling
- Add a reference path tracer for validation and cinematics
- Add multi-bounce and importance sampling
- Add ray-traced-lighting debug visualization

## Ray-Traced Denoising

- Add a spatiotemporal denoiser for ray-traced signals
- Add separate denoisers for reflections, shadows, and global illumination
- Add temporal accumulation with history rejection
- Add variance-guided spatial filtering
- Add disocclusion handling
- Add firefly suppression
- Add denoiser quality presets
- Add denoiser debug visualization

## Virtual Shadow Maps

- Add virtual shadow maps with a page table
- Add on-demand shadow page allocation and residency
- Add per-pixel shadow resolution matched to screen density
- Add caching of static shadow pages
- Add invalidation on light or caster movement
- Add clip-map or cascade integration for directional lights
- Add page-pool budgets and eviction
- Add virtual-shadow-map debug visualization

## Advanced GPU Shading Features

- Add variable-rate shading with per-tile and per-material rates
- Add content-adaptive and motion-adaptive shading rates
- Add a mesh and amplification shader pipeline
- Add meshlet rendering through mesh shaders
- Add GPU work graphs for GPU-driven work expansion
- Add bindless resources and descriptor indexing
- Add a wave and subgroup intrinsics abstraction
- Add sampler-feedback-driven texture streaming
- Add streaming virtual textures for large material sets
- Add capability gating and fallbacks for each feature
- Add advanced-feature diagnostics

## Upscaling, Frame Generation & Reconstruction

- Add temporal super-resolution upscaling
- Add an integration layer for vendor hardware upscalers
- Add frame generation and interpolation
- Add reactive and transparency masks for reconstruction
- Add motion-vector and depth inputs for upscalers
- Add dynamic resolution feeding the upscaler
- Add sharpening and post-upscale filters
- Add latency management with frame generation
- Add quality and performance presets
- Add upscaling and frame-generation diagnostics

## HDR Display Output & Wide Gamut

- Add HDR display detection and capability query
- Add HDR10 and perceptual-quantizer output
- Add scRGB and extended-range output
- Add wide-gamut (Rec.2020 and DCI-P3) handling
- Add HDR tone-mapping and paper-white calibration
- Add an in-game HDR calibration screen
- Add UI and subtitle brightness handling in HDR
- Add graceful fallback to standard-dynamic-range output
- Add HDR-output validation and diagnostics

# Capability 20 · Core Services & Diagnostics

**Objective:** Deliver shared profiling, tracing, compression, cryptography, randomness, time, testing, benchmarking, and task-graph services that provide production diagnostics and reusable infrastructure to all subsystems.

## CPU Profiling, Tracing & Instrumentation

- Add named CPU profiling scopes
- Add a hierarchical frame timeline across subsystems
- Add a flame-graph and call-tree view
- Add per-thread timelines
- Add counters and custom plot values
- Add a low-overhead ring-buffer trace format
- Add export to standard trace viewers
- Add a live in-engine profiler overlay
- Add markers correlated with GPU timings
- Add allocation and lock instrumentation
- Add sampling and instrumented profiling modes
- Add profiler-cost budgets and toggles

## Compression Codecs

- Add a general-purpose fast compression codec
- Add a high-ratio compression codec
- Add streaming compression and decompression
- Add block and chunked compression for random access
- Add a codec abstraction with selectable backends
- Add hardware-accelerated decompression where available
- Add compression-level and dictionary configuration
- Add integration with assets, streaming, saves, and networking
- Add compression benchmarks and diagnostics

## Cryptography & Secure Hashing

- Add symmetric encryption for data at rest
- Add authenticated encryption with integrity tags
- Add secure cryptographic hashing
- Add signing and signature verification
- Add key derivation and secure random generation
- Add certificate and token handling
- Add tamper detection for saves and packaged content
- Add a secure-storage abstraction for secrets
- Add cryptography validation and test vectors

## Random & Noise Utilities

- Add a fast general-purpose pseudo-random generator
- Add multiple seedable random streams
- Add uniform, normal, and weighted distributions
- Add deterministic and reproducible sequences
- Add a shared noise library reused across systems
- Add random utilities exposed to gameplay and scripting

## Date, Time & Calendar

- Add wall-clock date and time types
- Add time-zone handling and conversion
- Add durations and interval arithmetic
- Add formatting and parsing of dates and times
- Add culture-aware date and time formatting
- Add real-world time queries for live features
- Add monotonic and calendar clock separation

## Unit-Test & Benchmark Framework

- Add an engine-wide unit-test framework
- Add assertions, fixtures, and parameterized tests
- Add a micro-benchmark framework with statistics
- Add a headless test runner
- Add test discovery and filtering
- Add golden-data and snapshot testing helpers
- Add continuous-integration reporting output
- Add flaky-test detection and retries
- Add coverage instrumentation hooks

## Task Dependency Graph

- Add a declarative task dependency graph over the job system
- Add fork-join and pipeline task patterns
- Add cross-frame and persistent task graphs
- Add priorities and affinities per task
- Add cancellation and error propagation
- Add graph visualization and profiling
- Add deterministic scheduling for tests

# Capability 21 · Asset Pipeline & Content Management

**Objective:** Deliver a versioned asset lifecycle from import and validation through registry, dependency tracking, editing, cooking, packaging, hot reload, migration, and runtime consumption.

## Import Framework

- Add a unified asset-import framework
- Add mesh import from standard interchange formats
- Add skeletal, animation, and morph import
- Add texture and image import with format detection
- Add audio and video import
- Add material and scene import from interchange formats
- Add per-importer settings and presets
- Add re-import that preserves overrides
- Add batch and folder import
- Add import validation and error reporting
- Add custom importer plugins
- Add import diagnostics and previews

## Asset Database & Registry

- Add a central asset registry keyed by stable identifiers
- Add identifier-to-asset resolution and loading
- Add reference counting and lifetime management
- Add dependency tracking between assets
- Add reverse-dependency and usage queries
- Add metadata and tags per asset
- Add asset search and filtering
- Add rename and move with reference fixup
- Add missing-asset and redirect handling
- Add asset validation and integrity checks
- Add asset versioning and migration
- Add registry diagnostics

## Content Browser

- Add a content browser with folders and thumbnails
- Add asset creation, duplication, and deletion
- Add drag-and-drop of assets into scenes and fields
- Add search, filters, and collections
- Add asset previews and inspection
- Add dependency and reference viewers
- Add favorites and recently used
- Add bulk operations and metadata editing
- Add source-control status indicators
- Add a beginner-friendly starter-content library

## Cooking & Build

- Add a cooking step that converts source assets to runtime formats
- Add per-platform asset cooking
- Add incremental cooking of changed assets
- Add a derived-data cache shared across builds
- Add asset bundling and packaging for shipping
- Add strip-and-optimize passes for runtime assets
- Add cook validation and reporting
- Add distributed and cached cooking

## Hot-Reload & Live Content

- Add source-asset file watching
- Add automatic re-import on source change
- Add hot-reload of textures, meshes, materials, and audio
- Add hot-reload of scenes and prefabs
- Add live update of running gameplay from asset changes
- Add safe fallback when a hot-reload fails
- Add hot-reload diagnostics

# Capability 22 · VFX / Particle System

**Objective:** Deliver an authorable CPU- and GPU-driven visual-effects system with deterministic spawning where required, scalable simulation and rendering, world integration, LOD, budgets, debugging, and reusable effect assets.

## Effect Graph & Emitters

- Add a node-based effect graph
- Add emitters with configurable spawn rate and bursts
- Add emitter shapes (point, sphere, box, cone, mesh, spline)
- Add multiple emitters per effect
- Add sub-emitters spawned from particle events
- Add exposed parameters driven by gameplay
- Add reusable effect templates and presets
- Add effect lifetime, looping, and one-shot modes
- Add effect graph versioning and migration
- Add effect-graph debugging

## Particle Modules

- Add initial and over-life position, velocity, and acceleration modules
- Add color, opacity, and size-over-life modules
- Add rotation and angular-velocity modules
- Add force, drag, gravity, and turbulence modules
- Add curl-noise and vector-field modules
- Add collision modules against depth and the world
- Add attractor and orbit modules
- Add scale-by-speed and stretch modules
- Add custom-expression modules
- Add module ordering and stacking

## GPU Particle Simulation

- Add GPU-simulated particles with large counts
- Add a CPU-simulated path for small or gameplay-critical effects
- Add persistent particle buffers and pooling
- Add GPU spawn and death lists
- Add indirect draw from simulated particles
- Add depth-buffer and distance-field collision on GPU
- Add sorting for transparent particles
- Add particle simulation budgets

## Particle Renderers

- Add sprite and billboard renderers
- Add mesh-particle renderers
- Add ribbon and trail renderers
- Add beam renderers
- Add light-emitting particles
- Add decal-spawning particles
- Add lit, unlit, and volumetric particle materials
- Add motion blur and soft-particle depth fade
- Add sprite-sheet and flipbook animation

## VFX Runtime, LOD & Integration

- Add effect spawning and pooling at runtime
- Add effect LOD by distance and screen coverage
- Add effect culling and budgets
- Add fixed-cost and scalable-quality effects
- Add attachment of effects to entities and sockets
- Add effect events driving gameplay and audio
- Add integration with physics, wind, and weather
- Add a scripting API for spawning and controlling effects
- Add an effect-authoring editor with live preview
- Add effect performance profiling
- Add effect debugging and visualization
- Add effect tests

# Capability 23 · Camera System (Gameplay)

**Objective:** Deliver a gameplay camera framework with composable modes, rigs, blending, collision, effects, deterministic control, debugging, and clean integration with rendering, input, animation, physics, UI, and replay.

## Camera Manager & Blending

- Add a gameplay camera manager
- Add a camera stack with priorities
- Add smooth blends between active cameras
- Add blend curves and durations
- Add per-camera settings (field of view, offset, lag)
- Add camera activation and deactivation events
- Add handoff to and from cinematic cameras
- Add multiple cameras for split-screen
- Add camera debug visualization

## Camera Modes

- Add a first-person camera mode
- Add a third-person follow camera
- Add an orbit and free-look camera
- Add a top-down and isometric camera
- Add a side-scroll camera
- Add a fixed and rail camera
- Add a target-lock and look-at camera
- Add smooth transitions between modes
- Add per-mode tuning presets

## Camera Rigs, Collision & Effects

- Add a camera boom/arm with configurable length
- Add camera collision that pulls in on obstacles
- Add spring and lag smoothing
- Add camera shake sources and profiles
- Add recoil, impulse, and hit-reaction shake
- Add auto-framing and composition rules
- Add dynamic field-of-view by speed and state
- Add camera-space screen effects (vignette on hit, speed lines)
- Add zoom and aim-down-sights transitions
- Add camera-rig authoring and presets
- Add a scripting API for cameras
- Add camera tests

# Capability 24 · XR / VR / AR

**Objective:** Deliver capability-driven XR runtime support with stereo rendering, tracked devices, interaction, comfort, spatial mapping, performance budgets, platform lifecycle handling, and graceful unsupported-platform behavior.

## XR Runtime & Devices

- Add an XR runtime integration through an open standard
- Add headset detection, session, and lifecycle
- Add head and device pose tracking
- Add multiple XR device backends
- Add reference spaces (seated, standing, room-scale)
- Add recentering and origin management
- Add XR capability negotiation
- Add XR diagnostics

## Stereo Rendering & Comfort

- Add stereo rendering for two eyes
- Add single-pass and instanced stereo
- Add per-eye projection and lens correction
- Add foveated rendering where supported
- Add motion reprojection and late-latching
- Add comfort vignetting during locomotion
- Add stable framerate safeguards
- Add stereo-rendering diagnostics

## XR Input & Interaction

- Add XR controller input and bindings
- Add hand-tracking input
- Add gaze and eye-tracking input
- Add near and far interaction (grab, poke, ray)
- Add XR UI panels and pointers
- Add haptics for XR controllers
- Add teleport and smooth XR locomotion
- Add interaction debugging

## AR & Spatial Mapping

- Add camera passthrough for mixed reality
- Add plane and surface detection
- Add spatial anchors and persistence
- Add environment meshing and occlusion
- Add light estimation for AR
- Add image and object tracking
- Add AR content placement and scaling
- Add AR debugging and tests

# Capability 25 · Online Services & Platform Backend

**Objective:** Deliver platform-neutral identity, social, session, progression, commerce, content-delivery, and backend-service contracts with secure credentials, offline behavior, testing, and platform-specific adapters.

## Accounts & Identity

- Add a player account and identity service
- Add authentication and login flows
- Add linked platform identities
- Add session tokens and refresh
- Add user profiles and settings sync
- Add privacy and data controls
- Add account-service diagnostics

## Achievements, Leaderboards & Progression

- Add an achievements and trophies system
- Add progress-based and incremental achievements
- Add leaderboards with scopes (global, friends, region)
- Add stats and progression persistence
- Add player levels, unlocks, and rewards
- Add cloud saves with conflict resolution
- Add cross-device save sync
- Add progression-service diagnostics

## Presence, Friends & Social

- Add a rich-presence service
- Add a friends list and requests
- Add party and group management
- Add invites and join-in-progress
- Add guilds, clans, and player groups
- Add player mail and gifting
- Add text chat channels
- Add text and voice moderation and reporting
- Add a reputation and trust service
- Add blocking and privacy controls
- Add social-service diagnostics

## Commerce & Content Delivery

- Add a storefront and catalog
- Add in-app purchases and microtransactions
- Add virtual currency and wallets
- Add entitlements and ownership checks
- Add downloadable-content management
- Add receipt validation and fraud checks
- Add a content-delivery and patching pipeline
- Add delta and background updates
- Add commerce diagnostics and compliance

## Platform Integration & Backend

- Add a platform online-services abstraction
- Add integration adapters for each platform's services
- Add a backend web-service layer (request/response APIs)
- Add a persistence layer for accounts, inventory, and world state
- Add server-authoritative validation of online actions
- Add rate limiting and abuse protection
- Add a fallback offline mode
- Add backend scaling and health monitoring
- Add online-services tests

# Capability 26 · LiveOps, Analytics & Telemetry

**Objective:** Deliver privacy-aware analytics, crash and performance reporting, remote configuration, controlled rollout, operational diagnostics, and safe LiveOps integration without coupling core runtime behavior to service availability.

## Analytics Pipeline

- Add a game-event analytics pipeline
- Add typed custom events with parameters
- Add session, funnel, and retention tracking
- Add batching and offline event queuing
- Add sampling and rate control
- Add live dashboards and reporting hooks
- Add privacy, consent, and anonymization
- Add analytics validation and diagnostics

## Crash & Performance Reporting

- Add crash reporting with minidumps
- Add symbol upload and stack symbolication
- Add breadcrumbs and context capture
- Add automatic grouping and deduplication of crashes
- Add non-fatal error and assertion reporting
- Add performance telemetry (frame time, hitches, memory)
- Add device and hardware reporting
- Add a reporting dashboard and alerting

## LiveOps & Remote Configuration

- Add remote configuration and feature flags
- Add A/B testing and experiment assignment
- Add live-event scheduling and rotation
- Add content hotfix and data-patch delivery
- Add segmentation and targeting of players
- Add kill switches for problematic features
- Add remote-config rollout and rollback
- Add LiveOps diagnostics and audit logging

# Capability 27 · Replay & Spectator

**Objective:** Deliver deterministic replay recording and playback plus spectator and broadcast workflows with versioned data, seeking, validation, synchronization, scalable storage, and debugging integration.

## Replay Recording & Playback

- Add deterministic game-state replay recording
- Add a compact recorded-timeline format
- Add playback with pause, scrub, and speed control
- Add seeking with keyframes for fast navigation
- Add recording of input, events, and cosmetic state
- Add versioned replay compatibility handling
- Add streaming of long replays
- Add replay export and sharing
- Add replay validation and diagnostics

## Spectator & Broadcast

- Add a spectator mode with free and follow cameras
- Add observer delay for competitive integrity
- Add player and event tracking cameras
- Add a killcam and highlight capture
- Add director and auto-spectator camera logic
- Add broadcast overlays and data feeds
- Add multi-viewer synchronized spectating
- Add spectator and broadcast tests

# Capability 28 · Editor Framework & Tooling

**Objective:** Deliver the shared editor shell, command and transaction model, property inspection, viewport manipulation, developer tools, extension points, version-control workflows, and reliable collaborative authoring foundations.

## Editor Shell & Framework

- Add a shared editor shell with dockable panels
- Add saved layouts and workspaces
- Add a unified command and shortcut system
- Add a central undo/redo command history
- Add transactional multi-step edits
- Add a property inspector driven by reflection
- Add multi-object editing and reset-to-default
- Add an editor notification and progress system
- Add editor extensibility and plugin scripting
- Add editor preferences and settings

## Viewport Manipulation & Placement

- Add transform gizmos for translate, rotate, and scale
- Add world, local, and pivot space handling
- Add selection, multi-selection, and picking
- Add box and lasso selection
- Add grid, vertex, and surface snapping
- Add align, distribute, and pivot tools
- Add duplicate, group, and parent operations
- Add viewport navigation and camera controls
- Add viewport bookmarks
- Add measurement and ruler tools
- Add blockout and greybox geometry authoring
- Add a general spline and path authoring tool with mesh deformation

## Debug Console & Developer Tools

- Add an in-game developer console with command entry
- Add a command and cheat registry
- Add console-variable get and set at runtime
- Add an in-game debug menu for toggles and tweaks
- Add a unified gameplay debugger with per-entity inspection
- Add a unified debug-draw API (lines, shapes, text, screen)
- Add on-screen stat overlays and live graphs
- Add an immediate-mode debug UI for tools and panels
- Add spawn, teleport, and time controls
- Add a screenshot and capture tool
- Add debug-tool access control for shipping builds

## Version Control & Collaboration

- Add version-control integration for assets and code
- Add source-control status and operations in the editor
- Add asset locking to prevent conflicts
- Add change visualization and history
- Add merge and conflict-resolution tooling
- Add a review and annotation workflow
- Add multi-user live collaboration
- Add team presence and edit awareness
- Add collaboration diagnostics

# Capability 29 · Build, Packaging & Deployment

**Objective:** Deliver reproducible build, cook, package, patch, signing, distribution, and smoke-validation pipelines that produce traceable runtime and editor artifacts for every supported target.

## Build Pipeline

- Add a build pipeline that produces runnable targets
- Add configuration selection (debug, development, shipping)
- Add incremental and cached builds
- Add a data-cook and content-build stage
- Add shader compilation for each target
- Add build validation and pre-flight checks
- Add build artifacts and manifests
- Add reproducible builds

## Platform Targets

- Add desktop targets (Windows, Linux, macOS)
- Add mobile targets
- Add console targets
- Add a web target
- Add per-target feature and quality profiles
- Add cross-compilation and toolchain management
- Add per-platform packaging and installers
- Add platform capability manifests

## Packaging, Patching & Distribution

- Add packaging into distributable builds
- Add content pak and archive generation
- Add delta patching and updates
- Add downloadable-content packaging
- Add code and package signing
- Add store-submission preparation and metadata
- Add a continuous-integration and delivery integration
- Add automated smoke tests on packaged builds
- Add packaging and patch diagnostics

# Capability 30 · Advanced Audio & Acoustics

**Objective:** Extend the audio foundation with scalable environmental acoustics, propagation, speech processing, advanced mixing, platform-aware quality tiers, diagnostics, and authoring workflows.

## Acoustics Simulation

- Add ray-traced acoustics for reflections and occlusion
- Add wave-based acoustics for low frequencies where affordable
- Add precomputed and baked acoustic probes
- Add baked reverb and decay per region
- Add dynamic acoustic adaptation to geometry changes
- Add acoustic materials and absorption per surface
- Add acoustics cost budgets and quality scaling
- Add acoustics debug visualization

## Audio Scaling & Speech

- Add audio source LOD tiers by distance and importance
- Add aggregation of many sources into composite voices
- Add text-to-speech synthesis for UI and accessibility
- Add speech recognition and voice-command input
- Add output-mode presets (headphones, TV, night mode)
- Add dynamic-range compression profiles
- Add personalized and head-tracked HRTF
- Add directional sound indicators for accessibility
- Add a mono downmix option
- Add advanced-audio diagnostics and tests

# Capability 31 · Environment & Gameplay Extensions

**Objective:** Deliver higher-order environment and gameplay extensions that build on stable terrain, foliage, weather, water, procedural-generation, tagging, and ability-system contracts without duplicating subsystem ownership.

## Terrain Virtual Texturing & Runtime Deformation

- Add runtime virtual texturing for terrain material layers
- Add removal of the hard layer-count limit via virtual texturing
- Add baked and procedural mega-texture paths
- Add runtime terrain deformation (craters, tracks, footprints)
- Add persistent displacement trails in snow and sand
- Add hardware tessellation and adaptive displacement of terrain
- Add baked terrain self-shadow and horizon maps
- Add deformation networking and persistence
- Add terrain-extension diagnostics

## Foliage & Vegetation Extensions

- Add a procedural plant and tree modeler
- Add branch-hierarchy and pivot-painter wind baking
- Add runtime growth, spreading, and regrowth over gameplay time
- Add foliage contribution to global illumination and bounce
- Add emissive and seasonal-fruit variation
- Add foliage-extension diagnostics

## Weather & Sky Extensions

- Add severe-weather archetypes (tornado, sandstorm, blizzard whiteout)
- Add atmospheric optical phenomena (rainbows, halos, sun dogs, glories)
- Add from-orbit and space atmosphere transitions
- Add localized extreme-weather hazards affecting gameplay
- Add weather-extension diagnostics

## Water Extensions

- Add volumetric and dynamic-volume water (flooding, pouring, container fill)
- Add rising water level from accumulation and drainage
- Add wave-breaking and shorebreak geometry
- Add hull hydrodynamics for boats and ships (planing, drag, wake)
- Add coupling between the surface water and particle fluid simulation
- Add water-extension diagnostics

## Data & Narrative Procedural Generation

- Add procedural quest and mission generation
- Add procedural narrative and event generation
- Add loot-table and reward generation
- Add name, text, and lore generation
- Add procedural character, face, and outfit generation
- Add example-based and learned content synthesis
- Add data-PCG authoring and validation

## Gameplay Tags & Ability Framework

- Add a hierarchical gameplay-tag registry and asset
- Add tag containers and tag-query expressions
- Add tag-based matching and filtering
- Add a cohesive ability framework tying costs, cooldowns, and effects
- Add gameplay-effect stacking, duration, and periodic application
- Add attribute modification and clamping through effects
- Add networked ability activation and prediction
- Add ability and tag authoring tools
- Add ability-framework tests

# Capability 32 · Asset Streaming, Packaging & Virtual Filesystem

**Objective:** Deliver bounded asynchronous asset loading, residency, bundles, mounting, packaging, compression, encryption, and virtual-file access with stable handles, cancellation, diagnostics, and failure recovery.

## Background Asset-Processing Service

- Add a background asset-processing service that watches source content
- Add automatic processing of source assets into cooked runtime formats
- Add dependency-driven reprocessing when a source or dependency changes
- Add a processing job queue with priorities
- Add incremental processing of only changed assets
- Add parallel processing across worker threads
- Add distributed and shared processing across machines
- Add a shared derived-data cache to skip redundant work
- Add per-asset-type processors (mesh, texture, audio, material)
- Add processing status, progress, and error reporting
- Add retry and recovery for failed processing
- Add processing diagnostics and logs

## Asset Streaming, Loading & Handles

- Add asynchronous asset loading off the main thread
- Add addressable handles that reference assets by id or label
- Add load-by-address and load-by-label requests
- Add reference-counted load and release
- Add automatic loading of an asset's dependencies
- Add load priorities and queue ordering
- Add load progress reporting and callbacks
- Add cancellation of in-flight loads
- Add preloading and warmup of critical assets
- Add lazy and on-demand loading
- Add sub-asset and partial loading
- Add streaming of large assets (mesh, texture, audio) in chunks
- Add memory budgets and residency tracking for loaded assets
- Add eviction of unused assets under memory pressure
- Add placeholder and fallback assets while loading
- Add deterministic load ordering for tests
- Add a scripting API for loading and releasing assets
- Add streaming and loading diagnostics

## Asset Groups & Bundles

- Add asset groups that bundle related content
- Add labels and tags for addressing groups
- Add per-group build into loadable bundles
- Add local and remote (downloadable) group placement
- Add per-group compression and encryption settings
- Add cross-group dependency handling and deduplication
- Add a content catalog mapping addresses to bundles
- Add catalog updates for downloadable and patched content
- Add bundle load, unload, and reference counting
- Add bundle versioning and compatibility checks
- Add group and bundle authoring in the editor
- Add bundle build diagnostics and size reports

## Virtual Filesystem & Mounting

- Add a virtual filesystem over loose files and archives
- Add mount points with configurable priority
- Add mounting of pak/pack archive files
- Add overlay mounts where later mounts override earlier ones
- Add virtual-path resolution across all mounts
- Add loose-files-in-editor and archives-in-shipping modes
- Add read-only and writable mounts
- Add patch and downloadable-content overlay mounts
- Add case-insensitive and normalized path handling
- Add enumeration and globbing across mounts
- Add async file reads through the virtual filesystem
- Add memory-mapped reads for aligned archive entries
- Add mount lifecycle and hot-mount at runtime
- Add virtual-filesystem diagnostics

## Packaging, Compression & Encryption

- Add generation of pak/pack archive files from cooked content
- Add a directory index for fast lookup within archives
- Add per-file compression with selectable codecs
- Add block-based compression for random access
- Add entry alignment for memory-mapping
- Add encryption and signing of packaged content
- Add integrity checksums per entry and per archive
- Add chunked archives split by size or content group
- Add ordering of entries by load pattern for locality
- Add delta and patch archive generation
- Add on-demand and streamed archive download
- Add package validation and repair
- Add packaging size and compression reports
- Add packaging diagnostics

## Testing & Validation

- Add asynchronous-load correctness and cancellation tests
- Add reference-counting and eviction tests
- Add bundle build and dependency-deduplication tests
- Add virtual-filesystem path-resolution and override tests
- Add compression and decompression round-trip tests
- Add encryption and integrity-verification tests
- Add patch-overlay and catalog-update tests
- Add missing-asset and fallback-handling tests
- Add streaming memory-budget stress tests
- Add deterministic-load regression tests

# Capability 33 · Editor Tooling, Profiling & Automation

**Objective:** Deliver extensible production tooling for live code, memory and frame analysis, trace inspection, automation, functional testing, source control, and failure-resistant editor customization.

## Editor Extensions & Custom Tools

- Add custom editor windows and panels from plugins
- Add custom property drawers per type in the inspector
- Add custom asset editors for new asset types
- Add menu, toolbar, and context-menu extension points
- Add asset context actions and right-click commands
- Add creation wizards and guided flows
- Add editor modes with dedicated tools and viewport interaction
- Add an editor scripting and automation API
- Add editor commands registerable by tools
- Add editor-tool packaging and distribution
- Add reload of editor tools without a restart
- Add discovery and enable/disable of editor extensions
- Add documentation and metadata for custom tools
- Add sandboxing so a broken tool cannot crash the editor

## Live Coding & Native Hot-Reload

- Add recompilation of changed native code while running
- Add patching of the running process with new code
- Add hot-reload of native gameplay and tool code
- Add preservation of live state across a code patch
- Add safe rollback when a patch fails to apply
- Add incremental compile for fast iteration
- Add function-level and object-level patching
- Add integration with external compilers and IDEs
- Add notification and status of live-coding sessions
- Add exclusion of code that cannot be safely patched
- Add live-coding diagnostics and logs
- Add a fallback full-rebuild path

## Memory Profiler

- Add memory snapshots of the whole process
- Add snapshot comparison and diff over time
- Add per-tag and per-subsystem memory breakdown
- Add per-asset and per-resource attribution
- Add allocation call-stack capture
- Add leak detection and unfreed-allocation reports
- Add a live memory usage graph
- Add allocation-count and fragmentation views
- Add integration with memory budgets and warnings
- Add export of memory reports
- Add remote memory capture from a running game
- Add memory-profiler diagnostics

## Frame & Render Debugger

- Add capture of a single rendered frame
- Add a draw-call list with step-through
- Add inspection of bound resources per draw
- Add inspection of render targets and intermediate buffers
- Add inspection of shaders, states, and bindings per draw
- Add resource-content viewing (textures, buffers)
- Add pixel history for a selected pixel
- Add per-draw and per-pass timing
- Add render-graph and pass visualization in the capture
- Add shader input and output inspection
- Add capture from a running device or remote build
- Add comparison of two frame captures
- Add export of captures for sharing
- Add integration with external graphics debuggers

## Unified Session Profiler & Trace Analysis

- Add recording of a trace session across subsystems
- Add correlated CPU, GPU, memory, and event timelines
- Add network and asset-load events in the trace
- Add loading and offline analysis of trace files
- Add zoom, filter, and search across a trace
- Add statistics, aggregation, and hot-path detection
- Add comparison of two sessions for regressions
- Add remote capture from a running game
- Add markers and annotations from gameplay
- Add continuous background tracing with a ring buffer
- Add export and sharing of trace sessions
- Add machine-readable trace output for CI

## Functional & Automation Testing

- Add a functional-test framework that drives the running game
- Add headless execution of automated tests
- Add scripted playthroughs with input injection
- Add assertions on game and world state
- Add screenshot and golden-image comparison tests
- Add soak and endurance test automation
- Add stress and load test scenarios
- Add performance-regression gates in automation
- Add replay-based deterministic tests
- Add editor UI automation tests
- Add multi-client and networked test orchestration
- Add test scheduling across devices and platforms
- Add test artifacts (logs, screenshots, traces, videos)
- Add flaky-test detection and quarantine
- Add continuous-integration orchestration and reporting
- Add a test dashboard with history and trends

## Version Control Depth

- Add adapters for distributed and centralized version-control backends
- Add checkout, add, delete, move, and revert of assets
- Add changelists and grouped submissions
- Add submit, sync, and update operations
- Add history, blame, and revision inspection
- Add binary-asset diff and visual comparison
- Add merge and conflict resolution for assets
- Add large-file and binary-asset handling
- Add exclusive checkout and lock status
- Add offline operation and later reconciliation

## Testing & Validation

- Add tests for editor commands and undo/redo integrity
- Add tests for custom-tool registration and sandboxing
- Add live-coding patch-and-rollback tests
- Add memory-profiler snapshot and diff tests
- Add frame-capture and inspection tests
- Add trace-recording and analysis tests
- Add version-control operation tests
- Add functional-test-framework self-tests

# Capability 34 · Platform Support & Cross-Compilation

**Objective:** Deliver explicit runtime, toolchain, rendering, input, storage, packaging, certification, and automated-test support for each declared desktop, mobile, console, and web target.

## Platform Runtime Services

- Add a per-platform application entry point and lifecycle
- Add suspend, resume, focus-loss, and low-memory handling
- Add per-platform user-data, config, cache, and temp paths
- Add per-platform save-data storage and quotas
- Add per-platform system dialogs and message boxes
- Add per-platform clipboard and system integration
- Add per-platform locale and region detection
- Add per-platform power, thermal, and battery state
- Add per-platform display, refresh-rate, and HDR capability query
- Add per-platform network reachability
- Add per-platform notifications and system events
- Add a capability manifest per platform

## Windows Platform

- Add a Direct3D and Vulkan backend selection on Windows
- Add window creation, DPI awareness, and multi-monitor
- Add keyboard, mouse, and raw-input handling
- Add controller support (XInput and general gamepad)
- Add HDR output on capable displays
- Add borderless, fullscreen-exclusive, and windowed modes
- Add an installer package and silent-install support
- Add executable and package code signing
- Add crash minidump capture on Windows
- Add save and config storage in the user profile
- Add store-distribution packaging
- Add Windows platform tests

## Linux Platform

- Add a Vulkan backend on Linux
- Add X11 and Wayland windowing backends
- Add controller support via evdev
- Add file paths following the desktop base-directory spec
- Add packaging as a portable archive
- Add packaging as a self-contained app image
- Add packaging for common distribution package managers
- Add distribution and driver compatibility handling
- Add crash reporting on Linux
- Add fractional-scaling and multi-monitor support
- Add Linux platform tests

## macOS Platform

- Add a Metal backend on macOS
- Add Cocoa windowing and event handling
- Add universal binaries for Apple Silicon and Intel
- Add controller support via the platform game-controller framework
- Add code signing and notarization
- Add app sandbox and entitlements
- Add packaging as a disk image and store build
- Add Retina and multi-display handling
- Add platform achievements and leaderboards integration
- Add crash reporting on macOS
- Add macOS platform tests

## Android Platform

- Add a Vulkan and OpenGL-ES backend on Android
- Add app-bundle and split-ABI packaging
- Add the native activity and lifecycle bridge
- Add a native-to-Java interop layer
- Add touch, gesture, and gamepad input
- Add runtime permissions handling
- Add scoped and external storage handling
- Add the back-button and navigation handling
- Add platform game-services integration
- Add in-app-purchase billing integration
- Add push notifications
- Add device and GPU compatibility tiers
- Add thermal throttling and sustained-performance handling
- Add store-listing and asset-delivery packaging
- Add Android platform tests

## iOS Platform

- Add a Metal backend on iOS
- Add app packaging, provisioning, and signing
- Add store and test-distribution builds
- Add in-app-purchase integration
- Add platform game-services integration
- Add privacy and tracking-consent handling
- Add touch, gesture, and controller input
- Add the application lifecycle and background handling
- Add device tiers and thermal management
- Add safe-area and notch handling
- Add entitlements and capability configuration
- Add iOS platform tests

## Console Platforms

- Add a per-console graphics-backend integration
- Add console platform-SDK integration behind the abstraction
- Add controller and peripheral input per console
- Add console save-data and storage APIs
- Add user, account, and sign-in handling
- Add trophies, achievements, and presence per console
- Add suspend, resume, and quick-resume handling
- Add memory and performance constraints per console
- Add dev-kit deployment and debugging
- Add certification-requirement checklists per console
- Add store and package submission per console
- Add console platform tests

## Web & WebAssembly Platform

- Add a WebGPU rendering backend
- Add a WebGL fallback backend
- Add a WebAssembly build of the engine
- Add multi-threading via shared memory where available
- Add SIMD acceleration in the web build
- Add memory-limit handling and growth
- Add async fetch-based asset loading
- Add browser storage for saves and cache
- Add gamepad, keyboard, mouse, and touch input on the web
- Add fullscreen and pointer-lock handling
- Add load-time reduction and progressive streaming
- Add browser and device compatibility handling
- Add web platform tests

## Cross-Compilation & Toolchains

- Add a build configuration per target platform
- Add toolchain management for each platform
- Add platform SDK discovery and versioning
- Add conditional compilation per platform and feature
- Add a platform abstraction that isolates platform code
- Add cross-compilation from a single host where supported
- Add per-platform dependency and third-party management
- Add remote and cloud build execution
- Add reproducible cross-platform builds
- Add build caching across platforms
- Add per-platform shader and asset cooking
- Add automated multi-platform build verification

## Platform Certification & Compliance

- Add per-platform technical-requirement checklists
- Add automated checks for common certification failures
- Add age-rating and content-descriptor metadata
- Add accessibility-compliance checks per platform
- Add privacy and data-handling compliance
- Add store-metadata and asset requirements
- Add loading-time and performance requirement checks
- Add controller and input requirement checks
- Add a certification-readiness report

# Capability 35 · Monetization, Ads & Cloud Services

**Objective:** Deliver optional, isolated service modules for ads, commerce, cloud build, and remote streaming with secure backend validation, privacy compliance, cost controls, test environments, and no mandatory dependency from the engine core.

## Mobile Ads & Mediation

- Add a rewarded-video ad format
- Add interstitial ads
- Add banner ads
- Add native and playable ad formats
- Add an ad-mediation layer across networks
- Add waterfall and in-app-bidding ordering
- Add ad-network adapter integration
- Add fill-rate and revenue reporting
- Add ad frequency capping and pacing
- Add consent and privacy handling for ads
- Add child-directed and age-appropriate ad handling
- Add reward delivery and verification
- Add offline and no-fill fallback handling
- Add ad diagnostics and test modes

## In-App Purchases & Commerce Depth

- Add consumable, non-consumable, and subscription products
- Add a product catalog synced from the store
- Add purchase, restore, and refund handling
- Add receipt validation on a trusted backend
- Add entitlement granting and revocation
- Add subscription lifecycle and renewal handling
- Add promotional offers and discounts
- Add virtual-currency purchase and spend flows
- Add price localization and tax handling
- Add purchase-restoration across devices
- Add fraud detection and chargeback handling
- Add commerce analytics and reporting
- Add store-compliance and disclosure handling
- Add commerce test and sandbox modes

## Cloud Build & CI/CD

- Add cloud-hosted build execution
- Add build triggers from source-control changes
- Add parallel multi-platform build jobs
- Add a shared build and derived-data cache in the cloud
- Add artifact storage and retention
- Add automated test runs in the pipeline
- Add build status, notifications, and gating
- Add distribution to test and store channels
- Add versioning and build numbering
- Add secrets and signing-key management
- Add pipeline-as-code configuration
- Add build metrics and history

## Remote Rendering & Cloud Streaming

- Add server-side rendering of frames
- Add low-latency frame streaming to thin clients
- Add remote input capture and injection
- Add adaptive bitrate and resolution for streaming
- Add session management and scaling of stream instances
- Add browser and device clients for streamed play
- Add multi-user shared streamed sessions
- Add GPU allocation and pooling for stream servers
- Add encoding and transport optimization
- Add streaming diagnostics and quality metrics
- Add cost and capacity management
- Add streaming security and access control

# Capability 36 · 2D Subsystem

**Objective:** Deliver an integrated 2D production stack for sprites, atlases, tilemaps, lighting, cameras, physics, animation, effects, authoring, and batching while reusing shared engine services.

## 2D Rendering & Sprites

- Add a dedicated 2D rendering path
- Add sprite rendering with batching
- Add sprite pivots, flipping, and tinting
- Add sprite sorting layers and in-layer order
- Add a 2D draw order independent of 3D depth
- Add nine-slice and tiled sprite rendering
- Add sprite masking and stencil clipping
- Add 2D material and shader support
- Add additive, multiply, and custom 2D blend modes
- Add screen-space and world-space 2D rendering
- Add 2D render targets and post-processing
- Add 2D batching statistics and diagnostics

## Sprite Atlas & Assets

- Add a sprite-atlas asset that packs many sprites
- Add automatic atlas packing with padding
- Add sprite slicing from sheets (grid and auto-detect)
- Add per-sprite pivots, borders, and physics outlines
- Add multiple-atlas support and variants
- Add mip and filtering settings for pixel and smooth art
- Add atlas rebuild on source change
- Add atlas memory and packing reports

## Tilemaps

- Add a tilemap data structure and asset
- Add a tile palette and brush painting
- Add rectangular, isometric, and hexagonal grids
- Add multiple tilemap layers with ordering
- Add animated tiles
- Add rule tiles and auto-tiling
- Add random and weighted tile brushes
- Add tilemap collision generation
- Add chunked tilemaps for large levels
- Add tilemap streaming with the world
- Add a tilemap editor with paint, erase, fill, and select
- Add tilemap import and export
- Add tilemap rendering optimization and culling
- Add tilemap diagnostics

## 2D Lighting & Shadows

- Add 2D light sources (point, spot, directional, freeform)
- Add 2D normal-map lighting for sprites
- Add 2D shadow casting from shapes and sprites
- Add light blending and additive light layers
- Add global 2D ambient and day-night tinting
- Add light cookies and falloff shapes
- Add sprite self-illumination and emissive
- Add 2D lighting performance controls
- Add 2D lighting debug visualization

## 2D Cameras & Presentation

- Add an orthographic 2D camera
- Add pixel-perfect rendering and snapping
- Add resolution-independent 2D scaling
- Add camera follow, bounds, and dead zones
- Add parallax background layers
- Add 2D screen shake and effects
- Add multiple 2D cameras and split-screen
- Add safe-area handling for 2D UI overlap

## 2D Physics, Animation & Effects Integration

- Add integration with the 2D physics backend for sprites and tilemaps
- Add 2D collider generation from sprite outlines
- Add integration with 2D skeletal and cutout animation
- Add sprite-sheet flipbook animation playback
- Add 2D particle effects
- Add 2D trails and ribbons
- Add sorting integration between sprites, tilemaps, and particles
- Add 2D effect authoring

## 2D Authoring & Performance

- Add a 2D scene-editing mode
- Add sprite and tilemap placement tools
- Add sorting and layer management UI
- Add a 2D animation and state preview
- Add starter templates for platformers and top-down games
- Add 2D draw-call batching and atlas budgets
- Add 2D culling and off-screen skipping
- Add 2D performance profiling
- Add 2D subsystem tests

# Capability 37 · GPU Compute & Machine Learning Inference

**Objective:** Deliver a capability-gated GPU compute framework and versioned machine-learning inference runtime with explicit scheduling, resource ownership, batching, fallback behavior, profiling, and deployment constraints.

## GPU Compute Framework

- Add a general-purpose GPU compute dispatch API
- Add compute shaders authored and compiled per backend
- Add structured, raw, and typed compute buffers
- Add read, write, and read-write resource binding
- Add indirect dispatch from GPU-generated arguments
- Add shared-memory and workgroup configuration
- Add atomic operations and counters
- Add async compute queues alongside graphics
- Add barriers and resource-state transitions for compute
- Add readback of compute results to the CPU
- Add a capability check and CPU fallback
- Add compute profiling and diagnostics
- Add a scripting and gameplay API for compute jobs

## GPGPU Utilities

- Add parallel prefix-sum (scan)
- Add GPU sorting (radix and bitonic)
- Add parallel reduction
- Add stream compaction
- Add histogram generation
- Add matrix and vector math kernels
- Add image-processing kernels (blur, convolution, resample)
- Add noise and procedural-generation kernels
- Add a reusable compute-kernel library

## ML Inference Runtime

- Add a neural-network inference runtime
- Add import of models from an open interchange format
- Add a tensor type with shapes and data types
- Add a broad operator set for common model layers
- Add CPU inference with SIMD acceleration
- Add GPU inference via compute shaders
- Add hardware neural-accelerator use where available
- Add batching and streaming inference
- Add quantized and reduced-precision inference
- Add model loading, caching, and hot-swap
- Add async inference off the main thread
- Add memory and cost budgets for inference
- Add deterministic inference for tests
- Add inference diagnostics and profiling

## ML Integration & Use Cases

- Add inference-driven animation and deformation
- Add inference-driven behavior and decision making
- Add upscaling and denoising via learned models
- Add procedural content synthesis via models
- Add speech, text, and vision model integration
- Add a training and data-capture workflow
- Add model versioning and validation
- Add safety, fallback, and reproducibility controls
- Add ML-integration tests

# Capability 38 · Photo Mode, Capture & Recording

**Objective:** Deliver production photo, screenshot, video, and frame-capture workflows with deterministic presentation controls, high-resolution output, metadata, platform encoding, UI integration, and bounded runtime overhead.

## Photo Mode

- Add an in-game photo mode that pauses the world
- Add a free-fly photo camera with limits
- Add field-of-view, focal-length, and aperture controls
- Add depth-of-field and focus-point control
- Add exposure, white-balance, and color-grade controls
- Add filters, frames, and vignette overlays
- Add time-of-day and weather override in photo mode
- Add character pose, expression, and hide-player options
- Add composition guides (grid, level, horizon)
- Add sticker, logo, and caption overlays
- Add photo-mode presets and sharing
- Add photo-mode UI and controls

## Capture & Recording

- Add high-resolution screenshot capture
- Add super-resolution and multi-tile screenshots
- Add HDR and raw screenshot output
- Add video recording of gameplay
- Add configurable resolution, framerate, and bitrate
- Add audio capture synchronized with video
- Add a rolling background buffer for instant-replay clips
- Add highlight and clip capture triggered by events
- Add capture from replays with free camera and time control
- Add burst and timelapse capture
- Add watermark and metadata embedding
- Add export, gallery, and sharing
- Add capture performance budgets
- Add capture and recording tests

# Capability 39 · Renderer — Detailed Engineering Tasks

**Objective:** Close concrete implementation gaps in the renderer through code-level tasks with observable acceptance conditions, measured performance effects, regression coverage, and alignment with the renderer architecture established in Sprints 02 and 19.

## GPU Foundation, RHI & Bindless

- Implement a bindless resource layer that uploads all material albedo/normal/ORM textures into a single large descriptor array (or a texture-array atlas keyed by a per-instance texture index) so a full GBuffer pass can be drawn with one program and zero per-draw sampler rebinds, and report the achieved reduction in sampler-set calls in the submit stats.
- Add a persistent per-frame structured instance-data GPU buffer holding model matrix, previous-frame matrix, material index, and bounds for every proxy, indexed by instance id, so vertex shaders read transforms from the buffer instead of per-draw uniforms and the per-instance uniform upload path is eliminated.
- Introduce GPU timestamp query capture around each render-pass view and surface per-pass GPU milliseconds in the pass profile, so the pass-cost debug view reports measured GPU time rather than only estimated target bytes.
- Implement double- and triple-buffered upload ring buffers for the instance, light, and cull buffers with explicit frame-fence tracking, so dynamic buffers are never CPU-stalled waiting on in-flight GPU reads.

## GPU-Driven Rendering & Culling

- Build a Hi-Z depth pyramid pass that seeds from the current depth buffer and downsamples into a full mip chain, exposing the pyramid as a graph resource consumed by the cull pass.
- Implement a two-pass GPU occlusion cull that tests each instance's screen-space bounds against last frame's Hi-Z pyramid, draws the passing set, then re-tests false negatives against the current-frame Hi-Z and re-queues newly disoccluded instances so nothing pops in one frame.
- Replace the CPU-parity stub in the GPU-driven recorder with a real indirect execution path that writes draw-indirect structs from the compute visible list and submits with an indirect buffer, so visible-instance counts drive draws entirely on the GPU with no CPU readback.
- Implement GPU meshlet cluster culling by wiring the meshlet cull shader to test per-meshlet bounding cones and spheres against the frustum and Hi-Z, compacting surviving meshlets into an indirect draw list, and rasterizing them through the instanced path.
- Add a compute LOD-selection pass that picks each instance's LOD index from projected screen-space bounds and writes it into the indirect draw arguments, replacing any CPU LOD choice and reporting the selection count from GPU results.
- Implement per-instance GPU sorting of the visible list into front-to-back opaque and back-to-front transparent key order via a compute radix sort, so draw submission consumes an already-sorted indirect buffer instead of CPU sort keys.

## Shadows

- Implement cascaded shadow maps for the directional light by computing N log-uniform split frustums, rendering each into an atlas slice, and selecting/blending cascades per pixel in the deferred lighting shader, removing the current single-projection limitation.
- Implement a virtual shadow map system that allocates small shadow tiles into a sparse page pool addressed by a clipmap, marks pages dirty from moved casters and lights, and re-renders only dirty pages so shadow resolution tracks screen pixels without a fixed map size.
- Add point-light shadows via cube or dual-paraboloid depth rendering packed into the shadow atlas, and sample them in the lighting shader so point lights beyond the first slot cast shadows.
- Add spot-light shadows by rendering a perspective depth map per shadowing spot into an atlas slot and applying filtering in the lighting pass, extending shadowing beyond the single directional light.
- Implement the EVSM shadow filter path (currently only enumerated) by rendering moments into a float map, blurring them, and reconstructing visibility with Chebyshev bounds in the shader.
- Implement contact-hardening (PCSS) shadows (currently only enumerated) with a blocker-search plus penumbra-estimate filter kernel driven by light size.
- Implement screen-space contact shadows by ray-marching the depth buffer along the light vector for lights that request them, adding fine short-range occlusion the shadow map misses.

## Clustered & Tiled Lighting

- Implement a real clustered light-culling compute pass that builds a froxel grid, assigns each light to overlapping clusters into a per-cluster index list, and has the forward-plus and deferred shaders read the cluster list per pixel, so the clustered path culls lights instead of looping all of them.
- Implement analytic area-light shading using linearly-transformed cosines for rectangle, disk, and tube lights, which are currently packed but shaded as point lights, so they produce correct soft specular.
- Raise the light limit beyond the fixed uniform arrays by moving light data into a GPU structured buffer indexed by the cluster lists, so scenes with hundreds of lights render without the current cap.
- Add light-cookie and IES-profile support by sampling a projected texture or IES intensity map per spot and point light in the lighting shader, driven by a per-light texture index.

## Global Illumination & Reflections

- Wire the existing prefiltered environment, irradiance map, and BRDF LUT into the deferred and forward shaders so specular reflections and diffuse ambient come from real image-based lighting instead of the analytic hemisphere.
- Implement runtime prefiltering that convolves a captured or loaded cubemap into a GGX-prefiltered mip chain and an irradiance map on the GPU, and generate the split-sum BRDF LUT once at startup, producing the textures the IBL config expects.
- Implement parallax-corrected local reflection-probe blending by ray-intersecting the reflection vector against each probe's proxy volume and blending the nearest probes per pixel.
- Implement screen-space reflections as a Hi-Z ray-march over the GBuffer that produces a reflection color and confidence buffer and composites over IBL specular where the trace hits, falling back to probes on miss.
- Implement dynamic diffuse global illumination via an irradiance probe volume that traces or gathers radiance per probe, stores spherical-harmonic or octahedral irradiance, and samples it for indirect diffuse.
- Implement screen-space global illumination as a horizon-based indirect-bounce pass over the GBuffer that adds one bounce of colored indirect diffuse.
- Implement a baked-lightmap sampling path that reads a per-instance lightmap UV set and atlas texture for static diffuse global illumination.
- Implement ground-truth ambient occlusion as a compute pass over depth and normals producing a bent-normal and AO buffer that modulates IBL diffuse and specular.

## Materials & Advanced Shading Models

- Implement the subsurface-scattering shading model with a separable screen-space diffusion pass over a dedicated thickness/mask GBuffer channel, so skin and wax materials render translucently instead of as default-lit.
- Implement the clear-coat shading model by adding a second specular lobe with its own roughness and normal in the lighting shader, honoring the encoded shading model that is currently decoded but unused.
- Implement the cloth and hair shading models with sheen and Kajiya-Kay specular respectively, so the enumerated models produce distinct BRDFs instead of falling through to default-lit.
- Implement order-independent transparency for the transparent pass using per-pixel linked lists or weighted-blended OIT, so overlapping transparent surfaces composite correctly without CPU depth sorting.
- Add refraction and transmission for thin-translucent and single-layer-water models by sampling the scene color buffer with a roughness-driven blur offset by the surface normal, producing glass and water refraction.
- Add GPU per-object material-batch sorting that groups draws by pipeline-state key into contiguous ranges and issues one multidraw-indirect per state bucket, minimizing state changes across the whole opaque pass.

## Post-Processing, Anti-Aliasing & Upscaling

- Implement a temporal super-resolution upscaler that renders the scene at a fraction of output resolution, accumulates jittered samples with the existing motion vectors, and reconstructs full-resolution output with disocclusion rejection.
- Implement screen-space depth-of-field with a circle-of-confusion computation from the depth buffer plus a gather/bokeh blur pass driven by camera focal parameters.
- Implement per-object and camera motion blur that reconstructs blur from the motion-vector buffer already produced for temporal AA, tile-classifying max velocity and gathering along it.
- Add a color-grading pipeline that loads real 3D LUT assets into the color-grade slot and applies user grade strength, replacing the neutral-only LUT currently bound.
- Add lens post effects — chromatic aberration, vignette, and film grain — as a single parameterized fullscreen pass so final-image lens character is authorable.
- Implement physically based bloom with a progressive dual-filter mip pyramid replacing the single-radius blur, giving wide energy-conserving glare.
- Implement a physically based lens-flare and glare pass that thresholds bright HDR pixels and convolves them with a starburst and ghost kernel.

## Volumetrics, Atmosphere & Decals

- Implement volumetric fog as a froxel-grid compute pipeline that injects per-light scattering, ray-marches in-scattering with shadow sampling, and composites the result into the scene, activating light shafts that are currently flags-only.
- Implement a physically based sky-atmosphere model with transmittance and multiscatter lookup tables and aerial perspective, rendered before opaque and sampled by fog and IBL, giving time-of-day skies with no assets.
- Implement deferred decals that project material properties onto the GBuffer within decal box volumes during a dedicated pass between GBuffer and lighting, so surface detail can be layered without geometry.
- Implement height and exponential distance fog with a dedicated fullscreen pass that reconstructs world position from depth and blends a fog color, giving cheap atmospheric depth independent of the volumetric path.

## Frame Graph & Memory

- Implement physical transient-resource aliasing that allocates a single backing memory pool and binds aliased textures according to the compiler's existing aliasing plan, so the computed savings are realized instead of only estimated.
- Make the render pass set data-driven by allowing passes to be registered and inserted at runtime rather than a fixed enum, so decals, SSR, and volumetrics can add passes without editing the core enum.
- Emit explicit GPU barriers and transitions from the compiler's barrier list into the submission layer, so resource state transitions are graph-driven and validated rather than implicit in submit order.
- Add async-compute scheduling in the graph so independent compute passes (cull, Hi-Z, ambient occlusion, histogram) can run on a separate queue overlapping graphics work, reporting overlap in the pass profiles.

## HDR Output & Color Management

- Implement HDR display output that detects display HDR capability, requests a 10-bit or half-float backbuffer instead of the current LDR one, and applies the appropriate encode so tone mapping targets the display's real nit range.
- Implement a configurable display-referred tone-mapping curve with paper-white nits, peak nits, and hue-preserving desaturation in the output transform so HDR and SDR outputs share one grading pipeline with correct paper-white anchoring.

## Hardware Ray Tracing

- Add a ray-tracing acceleration-structure manager that builds and refits a per-mesh bottom-level structure and a per-frame scene top-level structure from the render proxies, exposed as a graph resource, gated on capability detection.
- Implement ray-traced shadows that trace visibility rays from GBuffer positions toward each shadowing light and denoise the result temporally, as an alternative to shadow maps where hardware supports it.
- Implement ray-traced reflections that trace from the GBuffer where screen-space reflections fail off-screen, shade hits with the deferred BRDF, and denoise, giving accurate off-screen reflections.
- Implement ray-traced ambient occlusion tracing short cosine-hemisphere rays against the acceleration structure as a higher-quality alternative to the screen-space pass.

## CPU Culling & Scene Systems

- Implement a two-level bounding-volume hierarchy or portal/occluder system on the CPU side of the scene extractor to reject whole sub-trees before per-instance GPU culling, reducing the instance count fed into the cull shader for large worlds.
- Add GPU-driven per-cluster small-triangle and back-face cone culling in the meshlet path so sub-pixel and back-facing meshlets are discarded before rasterization, cutting overdraw on dense meshes.
- Implement streaming virtual texturing that pages high-resolution material textures on demand based on GPU feedback of visible texel density, so large texture sets fit a fixed budget.

## Render Instrumentation

- Add a GPU-driven visibility and overdraw debug visualization that shades pixels by triangle or meshlet id or by overdraw count sourced from the GPU cull results, so culling correctness is inspectable.
- Implement a per-frame render-graph capture that serializes the compiled passes, barriers, aliases, and measured GPU timings to a file for offline inspection, making the graph's behavior verifiable in tests.

# Capability 40 · Engine Core — Detailed Engineering Tasks

**Objective:** Close concrete implementation gaps in engine core, ECS, reflection, serialization, assets, asynchronous execution, and scripting hosts with code-level verification and no parallel source of truth.

## Memory & Custom Allocators

- Implement a linear/bump arena allocator that carves aligned sub-allocations from a single reserved block, supports rewind-to-marker and reset, never frees individual allocations, and is covered by tests asserting pointer monotonicity and correct alignment for over-aligned requests.
- Implement a double-buffered per-frame allocator that hands out transient memory during a frame and is fully reset in constant time at the frame boundary by flipping buffers, with a test proving frame-N allocations are invalidated and reused in frame N+2.
- Implement a fixed-block pool allocator backed by a free-list of recycled slots that returns constant-time allocate and free, grows by chunk when exhausted, and is validated by a random allocate/free stress test without corruption.
- Implement a scoped stack allocator with RAII markers that asserts LIFO discipline in debug builds and releases everything above the marker on scope exit.
- Implement an engine memory tracker that records live bytes and allocation counts per string or enum tag, enforces per-tag budgets by failing or asserting when a tag exceeds its budget, and exposes a snapshot report queryable per tag.
- Provide standard polymorphic-memory-resource adapters for the arena, pool, and frame allocators so existing container usage in hot paths can be redirected to engine allocators without changing container types.
- Implement an aligned system-allocation wrapper that guarantees 64-byte alignment for SIMD and cache-line data on all compilers and backs the archetype chunk pool's raw pages.
- Back the archetype chunk pool with a virtual-address reserve/commit allocator that reserves a large contiguous region up front and commits pages on demand, exposing committed-versus-reserved bytes and reducing per-chunk system allocation count to near zero under steady state.

## Threading, Jobs & Lock-Free Primitives

- Extract the worker pool and job graph into a subsystem-agnostic jobs module out of the ECS namespace so renderer, asset streaming, and scene systems can submit work to one shared scheduler, with a test submitting non-ECS jobs.
- Add a typed task/future primitive with continuation chaining that schedules the continuation on the worker pool when the antecedent completes, without blocking a worker thread, validated by a chained-computation test.
- Add job priority levels to the worker pool queues so latency-critical jobs preempt background work at steal time, with a test asserting a high-priority job scheduled after a low-priority backlog starts first.
- Rewrite the job-graph runtime state to use atomic per-node dependency counters and a lock-free ready queue so completion callbacks perform no heap allocation and can be called concurrently from multiple workers, verified by a multi-threaded stress test.
- Implement a bounded lock-free multi-producer multi-consumer queue using a sequence-numbered ring buffer, and use it as the worker pool's global submission queue, covered by a stress test checking no lost or duplicated items.
- Implement a wait-free single-producer single-consumer ring buffer for one-to-one handoff such as render-command streaming, with a throughput test across two pinned threads.
- Implement a guided parallel-for on the shared job system that splits a range into work-stealing sub-ranges sized from remaining iterations so uneven per-item cost self-balances, verified against static striping on a skewed workload.
- Add epoch-based reclamation or hazard pointers so lock-free structures can free retired nodes safely, with a test that reclaims memory only after all readers have advanced their epoch.

## SIMD & Math

- Replace the scalar loops in the kernel float lanes with real intrinsic specializations for SSE2, AVX2, AVX-512, and NEON covering load, store, arithmetic, min, max, compare, and select, gated by the existing backend tags, with a test asserting bit-identical results against the scalar fallback.
- Add a hardware fused-multiply-add path that emits the intrinsic when available and documents the precision difference from the separate multiply-add fallback, selectable via a determinism flag.
- Implement a SIMD batch transform routine that multiplies an array of matrices against an array of positions using the intrinsic lanes, exposed as a kernel and benchmarked to beat the scalar path on large arrays.
- Add cache-line-aligned SIMD value types to the math library with load and store helpers so structure-of-arrays math buffers can be consumed directly by intrinsic kernels.
- Add matrix inverse, transpose, and translation-rotation-scale decompose functions to the math library so transform back-solves do not depend on the renderer's third-party math.
- Add perspective, orthographic, and look-at matrix builders to the math library under the documented handedness convention, with tests round-tripping a projected point.
- Add frustum construction from a view-projection matrix plus box and sphere intersection tests so scene culling has a native, testable primitive independent of the renderer.
- Add a fixed-point deterministic math option for lockstep-simulation cases where floating-point rounding differences across machines are unacceptable, with a cross-configuration bit-equality test.

## ECS Storage, Archetypes & Command Buffer

- Add an archetype transition-edge cache that memoizes the destination archetype for each source-archetype and add/remove-component pair so repeated structural changes skip archetype re-resolution, verified by a test asserting the second identical add is constant time.
- Extend the command buffer to support non-trivially-copyable components by recording per-type move-construct and destroy function pointers alongside the byte payload, so components holding handles or strings can be deferred, verified by round-tripping a move-only component.
- Add a bulk move-entities-between-worlds operation that transfers whole chunks by pointer swap when component layouts match instead of per-entity copy, validated by a test asserting zero per-entity component copies.
- Add a defragment maintenance pass that compacts partially-filled chunks of the same archetype into fewer full chunks during a sync point, reported via maintenance stats, with a test asserting reduced chunk count and preserved data.
- Add an order-preserving stable-removal option to entity destruction for archetypes flagged order-sensitive, with a test proving remaining entities keep their relative row order.

## ECS Queries & Scheduler

- Add a persistent cached query object that stores its matched-archetype set and invalidates incrementally on structural version bump, so repeated per-frame iteration skips archetype matching when no archetype was created or destroyed.
- Add query result change-filtering that iterates only chunks whose component version exceeds a caller-held baseline using the existing dirty ranges, with a test asserting untouched chunks are skipped.
- Add automatic per-system entity command buffers that the scheduler allocates, passes to each parallel worker, and plays back deterministically at the stage's sync point, so systems can request structural changes during parallel iteration without manual buffer wiring.
- Add work-stealing at query-chunk granularity within a single system so one wide system's chunks are distributed across all idle workers, verified by telemetry showing more than one worker active for a single system.

## World Resources, Relations & Lifecycle

- Add world-resource singleton components stored once per world outside archetype storage so global state such as time, input, and config is accessible to systems without a carrier entity, with get, set, has, and remove tests.
- Add relation cleanup policies that run when a relation target entity is destroyed so dangling relation pairs cannot survive, verified by destroying a target and asserting the chosen policy outcome.
- Add wildcard relation queries that enumerate all targets an entity relates to under a given relation, with a test listing multiple targets.
- Add exclusive-relation enforcement so a relation flagged exclusive replaces any existing target rather than adding a second pair, with a test asserting only the latest target remains.
- Add a deferred-destruction queue so entity destruction requested mid-iteration is recorded and applied at the next sync point, with a test proving iteration over an entity being destroyed remains valid until the sync point.

## Reflection & Type System

- Extend component reflection to describe nested struct fields recursively so an inspector or serializer can walk composite components, with a test reflecting a struct of structs.
- Add fixed-array and dynamic-container field descriptors to reflection with element type and count or stride so array members are enumerable, with a round-trip serialization test.
- Add string-field reflection with an offset and length/accessor strategy so text component fields can be inspected and serialized, with a read/write-by-reflection test.
- Add enum reflection that maps enum field values to enumerator names and back so inspectors show names and serializers store stable identifiers, with a name/value round-trip test.
- Add a general runtime type registry that reflects arbitrary non-component types such as assets and save structs using the same field-descriptor model, verified by reflecting a non-component struct.
- Add per-field attribute metadata such as min, max, step, tooltip, and hidden to reflection descriptors and expose it through the registry so editor tooling drives validation from a single source.

## Serialization, Snapshots & Save

- Make the chunked snapshot binary codec endian-portable by writing all multi-byte integers in a fixed little-endian layout and byte-swapping on big-endian hosts, verified by decoding an opposite-endian fixture.
- Add a component-schema version and field-layout table to the snapshot and component binary format plus a migration step that reorders, adds, or drops component fields when the stored layout differs from the runtime layout, verified by loading a fixture whose component gained and lost a field.
- Add optional block compression to the chunked snapshot codec with a header flag so large snapshots shrink on disk, verified by a compress/decompress round-trip and a size-reduction assertion.
- Add a whole-file checksum to the snapshot and save-game formats that is written on encode and validated on decode so corrupted files are rejected, with a bit-flip detection test.
- Extend save-game migrations with a value-transform migration kind that applies a caller-supplied function to convert a value's type or units between versions, with a transform-applied test.
- Add a component serialize/deserialize hook interface for components whose in-memory form differs from their persisted form, invoked by the snapshot restorer instead of a raw byte copy, verified by round-tripping a component holding an asset handle.

## Asset Management

- Add an asset dependency graph that records which assets reference which and propagates hot-reload to dependents when a base asset changes, verified by reloading a material and asserting a dependent mesh instance observes the update.
- Add an asynchronous streaming loader that enqueues load requests with priorities onto the shared job system and resolves handles on completion, so callers request-then-poll without blocking, with a priority-ordering test.
- Add a budget-driven cache eviction policy that unloads least-recently-used unreferenced assets when a configured memory budget is exceeded, integrated with the memory tracker tags, verified by loading past budget and asserting the coldest asset is evicted.
- Add content-hash-addressable asset identity that deduplicates identical imported payloads to one cache entry keyed by content hash so two references to identical content share one resident copy, with a dedup-count test.
- Add a generational-index runtime handle alongside the shared-pointer handle so systems can store a trivially-copyable, revocable reference that reports staleness after reload or unload, with a stale-detection test.

## Coroutines, Async & Script Host

- Add a coroutine task type whose awaiter suspends onto the shared job system and resumes on a worker when the awaited job completes, giving native systems await-style async without blocking threads.
- Add a frame-yielding coroutine primitive for wait-next-frame and wait-seconds driven by the scene task loop so gameplay logic can span frames without hand-rolled poll state machines, with a multi-frame resume test.
- Add real Lua coroutine suspension by routing behaviour calls through new-thread and resume so a script yield suspends and resumes across frames instead of running to completion, verified by a script that yields with preserved locals.
- Add a persisted execution-position field and a wait node kind to the visual-graph runtime so a graph can suspend at a node and resume there next invocation, verified by a graph that pauses and continues from the same node.
- Add a structured cancellation token threaded through the async task and streaming-load APIs so an in-flight coroutine or load can be cancelled at its next suspension point and releases its resources, with a cancel-mid-flight test.

# Capability 41 · Scripting, Audio & Input — Detailed Engineering Tasks

**Objective:** Close concrete implementation gaps across scripting, audio, and input using production integrations, explicit runtime contracts, focused automated verification, and measurable real-time behavior.

## Scripting

- Implement an engine-integrated coroutine scheduler exposed to all script backends as wait-seconds, wait-frames, and wait-until-condition primitives that suspend a behaviour function mid-body (via yield on Lua and a resumable state machine on native and visual-graph) and resume it on the correct tick group, so sequential logic needs no manual state machines.
- Implement a script-facing Task.Start that accepts a script-authored coroutine body and drives it through the scene task system, completing the deferred half of the Task API so scripts, not only native code, can spawn, await, and cancel long-running asynchronous work.
- Add a per-behaviour instruction budget by installing a Lua count hook that aborts a script with a diagnostic once it exceeds a configurable opcode count per lifecycle invocation, so a runaway loop in one behaviour cannot hang the frame.
- Add a per-state or per-behaviour memory ceiling via a custom Lua allocator that fails allocation past a configurable byte cap and surfaces it as a script diagnostic, so a script cannot exhaust process memory.
- Implement hot-reload state preservation for Lua behaviours that serializes the previous environment's live exposed and script-declared persistent variables before the environment swap and re-injects matching values afterward, so editing a script mid-play does not reset gameplay state.
- Implement per-instance exposed-variable override application for the visual-graph backend so editor-authored overrides seed graph default-value pins before the created lifecycle, achieving parity with the Lua backend instead of the current no-op.
- Implement per-instance exposed-variable storage and override application for the native backend so compiled behaviours declare exposed fields addressable per entity, removing the native-backend no-op.
- Build a debug-adapter-protocol server that bridges the existing Lua debug hook's pause, step, breakpoint, and call-stack machinery to a socket endpoint so an external editor can attach, set breakpoints, step, and inspect frames over a standard protocol.
- Extend breakpoints with a condition expression and a hit-count field, evaluating the condition in the paused frame's environment inside the hook so a breakpoint only stops when its predicate is true or its Nth hit is reached.
- Add a watch and evaluate facility that compiles and runs an arbitrary expression string against a paused frame's locals and upvalues and returns a typed variable snapshot, so a debugger can evaluate expressions and inspect nested tables on demand.
- Add a set-variable capability that writes a new value into a named local or upvalue of a paused frame so a debugger can mutate state at a breakpoint.
- Introduce a managed C#/.NET script backend implementing the backend interface that loads a compiled assembly, maps lifecycle and event callbacks and the existing function-registry surface into managed method calls, and marshals script values across the boundary, adding a fourth first-class scripting language.
- Extend the script value type with array and string-keyed-map variants plus marshalling on every backend boundary, so structured data such as lists and records can cross the script boundary without being flattened into separate scalar pins.
- Add first-class vector, quaternion, and color value types to the script value system and function-registry pins so transform, physics, and renderer APIs pass composite math types as single arguments instead of three float pins.
- Add a per-behaviour and per-registered-function CPU-time profiler that samples execution duration during lifecycle, event, and call dispatch and exposes an aggregated per-frame report, so designers can find which scripts and API calls dominate the budget.
- Add a change-notification observer mechanism to the shared script state so a behaviour can subscribe to a key and be invoked when its value changes, replacing per-frame polling for cross-behaviour shared data.
- Implement native-plugin hot-reload state preservation that snapshots a native behaviour's exposed and registered instance data before unloading the shared library and restores it after the rebuilt plugin reloads.
- Add a sandbox capability policy per behaviour asset that gates which registered API namespaces a given script may call and rejects disallowed calls with a diagnostic, so content-authored scripts run with least privilege.
- Add structured error objects carrying source line, chunk, and call stack to script diagnostics emitted from failed safe calls, so runtime script errors surface a navigable location in tooling.
- Implement a deterministic script fixed-tick group that runs flagged behaviours on the physics fixed timestep with an accumulator, so gameplay logic requiring stable step size runs independently of render frame rate.

## Audio

- Build a per-bus DSP insert chain using a node graph so each authored mixer bus can host an ordered list of effect nodes routed between the bus source and its parent, turning buses into real processing chains rather than volume and mute only.
- Implement a low-pass filter effect node selectable per bus and per source with authorable cutoff, so environmental muffling and dialogue clarity can be shaped without pre-baked assets.
- Implement a reverb effect node with authorable room parameters as a shared aux bus plus per-source send levels, so multiple sources feed one reverb tail through a send-and-return topology.
- Implement a parametric equalizer effect node with multiple bands usable on any bus so mixes can be tone-shaped at authoring time.
- Implement a dynamics compressor and limiter effect node on the master and arbitrary buses with threshold, ratio, attack, and release parameters, so the mix is protected from clipping and can be glued.
- Add sidechain ducking that keys one bus's gain reduction off another bus's signal level so, for example, music is ducked by a dialogue bus each audio tick.
- Extend mixer snapshots beyond bus volumes to also carry per-bus mute, pitch, and effect-parameter overrides, and interpolate all of them during snapshot transitions, so a snapshot captures a full mix state.
- Add a per-clip streaming policy that selects stream-versus-decode-to-memory and honor it instead of unconditionally streaming, so short one-shot effects play from a decoded in-memory buffer without per-play disk streaming.
- Implement an in-memory decoded-PCM cache keyed by clip asset id so repeated one-shots of the same short clip share one decoded buffer instead of re-decoding per voice.
- Implement asynchronous clip loading and decoding on a worker so one-shot playback and source creation never block the game thread on first-touch decode, resolving the sound once decoding completes.
- Implement sample-accurate scheduled playback that starts a voice at a specified future audio-clock frame, exposed through the play descriptor, so cues fire exactly on the audio clock rather than at frame boundaries.
- Add beat and tempo-quantized scheduling built on the sample-accurate start-time mechanism so a cue can be scheduled to the next bar or beat of an authored tempo, enabling rhythmic music layering.
- Replace binary occlusion with a filtered occlusion model that additionally drives a per-voice low-pass cutoff proportional to ray-blocked coverage, so occluded sources are muffled and attenuated rather than only attenuated.
- Distinguish obstruction from occlusion by sampling the direct path and the reverb-send path separately, applying obstruction to the dry signal while leaving the reverb send audible, so a source behind a thin wall sounds correctly indirect.
- Add multi-listener support that drives multiple listeners bound to per-local-user cameras and routes each spatial source to the nearest listener, so split-screen local multiplayer gets correct per-viewport 3D audio.
- Add directional source cones with inner and outer angle and outer gain so sources such as speakers and character mouths radiate directionally instead of omnidirectionally.
- Implement reverb-zone volumes as scene components that select an environmental reverb preset by listener position and crossfade the master reverb parameters on zone transitions.
- Add per-bus and master metering that reports peak and RMS levels each audio tick through a lock-free channel to the game thread so editor meters and audio debugging can display live signal levels.
- Add an optional binaural HRTF spatialization path selectable per source so headphone users get true binaural positioning instead of only vector-based panning.
- Implement looping-voice virtualization that records stolen looping one-shots and automatically restarts them at their computed current position when pool capacity frees, so an important looping sound resumes instead of being permanently lost to voice stealing.
- Add a music crossfade helper that ties two voices to a shared normalized fade parameter and ramps their send volumes inversely over an authorable duration, so track transitions need no manual per-frame volume scripting.
- Add distance-based air-absorption filtering that scales a per-voice low-pass cutoff with listener distance so far-away sources lose high frequencies realistically, layered on top of distance attenuation.

## Input

- Implement a device-to-local-user binding layer that assigns a specific gamepad index and optionally the shared keyboard and mouse to each local user and routes only that device's state into that user's evaluation, so local multiplayer players control separate characters from separate controllers.
- Implement a press-any-button-to-join flow that watches all unassigned connected devices for a first actuation and returns the actuating device so it can be bound to a newly created local user, so drop-in local co-op works without pre-assignment.
- Emit device hotplug events into the script event bus by diffing gamepad connectivity across frames so gameplay can react to controllers being plugged in or lost instead of polling.
- Implement an interactive rebind capture flow that listens for the next actuated key across all devices with cancel and timeout, returns it as a candidate, and feeds it into the rebind path, so a settings screen can offer press-a-key-to-bind.
- Extend rebinding to address an individual composite slot via a binding-id and slot-index pair, closing the gap where a composite's slots cannot be independently rebound.
- Implement last-used-device tracking that records whether the most recent actuation came from keyboard and mouse or a specific gamepad and exposes it per local user so UI can switch button-prompt glyphs to match the active device.
- Implement the reserved field-of-view-scaling modifier by giving the input subsystem access to the active camera's field of view and scaling the value accordingly, converting the reserved enum slot into a working sensitivity-versus-zoom modifier.
- Add an input-buffering trigger option that remembers an actuation for a configurable window so a slightly-early press still satisfies the action when its gate opens later that window, enabling forgiving action-game timing.
- Implement a directional-sequence combo trigger that fires when a configured ordered sequence of actions or directions completes within a time budget, extending the trigger set beyond the single-action gate.
- Add an action-level toggle modifier that converts a momentary press into a latched on/off state resolved in the mapping evaluator, so accessibility and preference toggles need no per-behaviour scripting.
- Introduce named haptic effect assets describing a magnitude-over-time curve and dual-motor mix and a player that drives the vibration backend each frame from the curve, so designers author rumble patterns instead of only setting constant motor magnitudes.
- Implement per-local-user haptics routing that maps a local user to its bound device index so setting vibration addresses a specific player's controller rather than a raw device index.
- Add virtual on-screen touch control regions that map touch contacts to actions so mobile builds get button and stick input through the same action system, going beyond a single binary touch key.
- Add a mouse-delta relative-motion evaluation path distinct from absolute pointer position so first-person look uses frame-to-frame delta unaffected by cursor clamping or absolute resets.
- Implement per-device sensitivity and dead-zone calibration profiles that scale and curve a device's analog inputs before modifier evaluation and persist alongside the rebind profile so players can tune stick response per controller.
- Add a rebind-UI query API that enumerates every rebindable binding in the active contexts with its current key, display name, and conflict status so a settings screen can render the full remap table without walking asset internals.
- Implement an input-consumption report that records which higher-priority mapping context consumed a key this frame and exposes it so gameplay can detect when UI or console captured input rather than silently receiving nothing.
- Add a snapshot-diff comparison utility over input recordings that reports the first frame two recordings diverge in resolved action state, turning deterministic replay into a regression-testable golden-file mechanism for input logic.

# Capability 42 · Editor Tooling — Detailed Engineering Tasks

**Objective:** Close concrete editor workflow and reliability gaps with transactional behavior, persistence, recovery, diagnostics, automation, live-code support, and end-to-end authoring verification.

## Transform Gizmos, Snapping & Viewport Manipulation

- Implement a gizmo coordinate-space toggle that switches axis orientation between world-aligned and the selected entity's local rotation, persisted per-viewport and bound to a hotkey, so translating along local axes follows the object's frame.
- Implement scale-value snapping in the gizmo drag path that rounds each axis' scale delta to a configurable increment, exposed as a scale-snap toolbar dropdown and verified by a self-test that a dragged scale lands exactly on the increment.
- Implement vertex and surface snapping that, while a modifier key is held during a translate drag, raycasts the mouse against other meshes' triangles and vertices and snaps the dragged object's pivot to the nearest hit, so parts assemble precisely without manual coordinate entry.
- Implement a pivot-mode switch between bounding-box center, object origin, and median of multi-selection that changes where the gizmo renders and about which point rotate and scale operate.
- Implement a numeric transform-entry overlay that lets the user type an exact delta mid-drag to move, rotate, or scale by that amount along the active gizmo axis, committing as a single undo step.
- Implement a viewport statistics HUD that overlays live frame time, FPS, draw-call count, triangle count, and visible-entity count sourced from the submission builder, toggled from the viewport toolbar.
- Implement camera bookmarks that store named yaw, pitch, pivot, and distance poses to the workspace file and restore them with an animated transition through the existing focus path.
- Implement a selection-isolation solo mode that temporarily hides all non-selected entities in the viewport render pass and restores full visibility on exit, driven from the hierarchy and reversible without mutating the scene document.
- Implement viewport-to-file capture that renders the current viewport at a chosen resolution and writes an image to disk, wired to a toolbar button and usable headlessly for regression screenshots.

## Command System & Undo/Redo

- Implement drag-coalescing in the command stack so consecutive same-target transform commands issued within one gizmo gesture merge into a single reversible entry, verified by a test asserting a multi-frame drag collapses to one undo.
- Implement an undo-history panel that lists every entry in the active partition with its label and a marker at the current position and lets the user click any entry to undo or redo to that exact point deterministically.
- Implement a can-merge-with protocol on editor commands plus a time-window threshold so rapid inspector field edits coalesce while distinct edits remain separate undo steps.
- Implement a transaction and scope API on the command stack that groups an arbitrary set of sub-commands into one atomic compound command whose undo reverses all children in reverse order.
- Implement a per-scene dirty-since-last-save indicator derived from comparing the command-stack position against the position recorded at save time, so the title bar and close prompt reflect true modification state across undo and redo.

## Asset Database & Content Browser

- Implement a persistent asset database that assigns a stable identifier to every project asset, stores a serialized index of path, type, hash, and dependencies on disk, and incrementally updates it on file change, replacing the current directory-scan index.
- Implement a cross-type reference and dependency inspector that, for any selected asset, queries the database to list all assets that reference it and all assets it depends on, generalizing the material-only reference finder.
- Implement safe asset rename and move that rewrites all incoming references by identifier across scenes, materials, and prefabs in one transaction so relocating an asset never produces dangling links.
- Implement a broken-reference scanner that walks the asset database, reports every asset with a missing dependency, and surfaces results in a dockable list with click-to-select.
- Implement a content-browser search-and-filter bar that queries the asset database by name substring and asset-type facet and repopulates the tile and list view live, independent of the current folder.
- Implement an import-preset system that stores per-extension import settings as project files and applies them automatically during import so re-imports are deterministic.
- Implement a background thumbnail-generation queue that renders mesh, material, and texture thumbnails off the UI thread and streams them into the disk cache as they complete so browsing a large folder never blocks paint.
- Implement drag-and-drop of a mesh into the viewport that spawns the entity at the surface point under the cursor via raycast rather than the origin, as a single undoable command.

## Prefabs & Scene Authoring

- Implement a prefab override system that records per-instance property deltas against the source prefab, displays overridden fields with a distinct marker in the inspector, and supports per-property revert-to-prefab and apply-to-prefab.
- Implement nested-prefab editing so a prefab instance can be opened in an isolated edit context, changes written back to the prefab asset, and all other instances updated on reload.
- Implement a prefab and scene diff view that compares two files or an instance against its source and lists added, removed, and changed entities and components in a reviewable tree.
- Implement prefab variants that inherit from a base prefab and store only their overrides so a family of related objects can share edits pushed from the base.
- Implement multi-entity copy, paste, and duplicate-with-hierarchy that serializes the selected subtree and reinstantiates it as one undoable command, including paste-into-parent.

## Console & Diagnostics

- Implement a console command-input line with a parser and a registry of named editor commands plus command-history recall, turning the read-only log into an interactive command surface.
- Implement a console text-search filter that highlights and narrows entries matching a query string, combined with the existing level filters.
- Implement click-to-navigate on console entries so a log line carrying a source location opens the script editor at that line or selects that entity.
- Implement per-category console filtering built from the distinct category values already stored on each entry so noisy subsystems can be muted.

## Profiling, Capture & Debugging

- Implement a CPU frame-profiler panel that samples named scopes per frame from the message loop and rendering submission and renders a per-frame timeline breakdown with min, average, and max timings.
- Implement a memory-profiler panel that tracks editor and scene allocations by category and displays live totals plus a high-water mark, sourced from an instrumented allocator hook.
- Implement a GPU frame-capture debugger that snapshots one frame's draw calls with their render state, textures, and view targets and presents an inspectable per-draw-call list for diagnosing rendering issues.
- Implement a render-pass visualization selector in the viewport that overrides output to show albedo, normals, depth, overdraw, or wireframe through the existing submission path.
- Implement an asset-load telemetry log that records every asset load and cook duration and cache hit or miss and exposes it in a sortable diagnostics list.

## Live-Coding & Hot-Reload

- Implement a Lua script hot-reload watcher that detects changes to script assets on disk and reloads them into the running scene session without restarting play mode, preserving entity state where possible.
- Implement native behaviour and plugin hot-reload that unloads and reloads the compiled gameplay module and re-binds live component instances so native behaviour changes take effect without relaunching the editor.
- Implement a live reimport-on-change pipeline that watches source files and re-cooks and reapplies them to open scenes automatically, extending the material-graph cook-reload behavior to all asset types.
- Implement play-mode frame stepping with single-step and step-N added alongside pause, advancing the simulation exactly one fixed tick per press for debugging gameplay frame-by-frame.

## Automation & Functional Test Harness

- Implement an input-driven automation harness that feeds synthetic pointer and keyboard event sequences into the live window message pipeline and asserts resulting editor state, enabling GUI-level regression tests beyond the object-level self-test.
- Implement an interaction record-and-playback system that captures a session's input events to a file and deterministically replays them against the editor so reported bugs can be reproduced in continuous integration.
- Implement a scriptable automation command layer that lets an external driver invoke editor operations over a local channel and read back results, enabling agent-driven end-to-end tests.
- Implement golden-image viewport comparison in the self-test harness that renders known scenes headlessly and diffs against stored reference images with a tolerance, failing on visual regressions.

## Persistence, Recovery & Version Control

- Implement periodic scene autosave that writes a timestamped recovery copy at a configurable interval and on focus loss and an on-launch recovery prompt that restores the latest copy after a crash.
- Implement version-control status integration that queries the working copy for each asset's state and overlays a status badge on content-browser tiles and hierarchy rows.
- Implement version-control actions to stage, revert, diff, and view history invocable from the asset context menu, operating through a pluggable backend interface so the concrete system is swappable.
- Implement a text-serialization canonicalization pass for scene and prefab files that produces stable, line-diffable output with sorted keys and deterministic ordering so merges and code review of scene changes are tractable.

## Workflow & Discoverability

- Implement a command palette invoked by a global shortcut that fuzzy-searches all named editor actions and assets and executes or opens the chosen result, providing keyboard-first navigation of the whole toolset.
- Implement a user-editable keymap that loads and saves keyboard bindings for every editor command to a config file and resolves conflicts, replacing the hard-coded shortcut policy.
- Implement a transient toast notification service that surfaces non-blocking success, warning, and error messages with auto-dismiss so feedback no longer relies solely on the console log.
- Implement entity tags and layers with a management panel, storing them on the scene document and enabling hierarchy filtering, bulk selection, and per-layer viewport visibility toggling.

# Capability 43 · Physics — Detailed Engineering Tasks

**Objective:** Close concrete physics backend and high-level simulation gaps with stable component contracts, deterministic boundaries, representative scenes, backend-accurate debugging, and performance verification.

## Collider Shapes & Geometry

- Implement a triangle-mesh static collider that bakes a collider component's mesh reference into a Jolt mesh shape with indexed triangles so arbitrary static level geometry generates contacts, verified by a sphere resting on a concave imported mesh floor.
- Implement a convex-hull dynamic collider that builds a Jolt convex-hull shape from a point cloud or source mesh vertices with vertex capping and simplification, verified by stacking two hull-shaped bodies that settle without interpenetration.
- Implement a heightfield terrain collider backed by a Jolt heightfield shape fed from a height sample grid, verified by a raycast and a rolling sphere tracking the sampled terrain profile.
- Add cylinder and tapered-capsule shape kinds mapping to the corresponding Jolt shapes, verified by creating each and confirming a body with the expected local bounds.
- Implement multi-shape compound colliders by allowing an entity to own several child collider descriptors combined into a Jolt compound shape, verified by a single body whose L-shaped compound blocks a ray through the concave notch.
- Rotate the collider center offset by the body rotation when composing the body position in body creation and debug draw so an off-center collider on a rotated entity is simulated at the correct world location, verified by a rotated box whose off-center shape contacts match its rendered wireframe.
- Implement per-shape convex-radius and margin configuration on the collider component mapping to Jolt's convex radius, verified by a thin box that no longer jitters when configured with a reduced margin.

## Rigid-Body Dynamics

- Add linear-damping and angular-damping fields to the rigidbody component wired to the body creation settings, verified by a spinning body whose angular velocity decays to a measured fraction over a fixed number of steps.
- Add per-axis position and rotation freeze flags mapped to individual allowed-degrees-of-freedom bits, replacing the all-or-nothing rotation lock, verified by a body that translates only on one axis while all rotation is locked.
- Add a center-of-mass override applied via the mass-properties override with a shifted center of mass, verified by a box that tips predictably around its overridden center of mass.
- Add an inertia-tensor and mass-distribution override so authored mass properties bypass automatic inertia calculation, verified by comparing angular response of a body with custom versus auto inertia under identical torque.
- Add configurable maximum linear and angular velocity clamps mapped to the body's velocity caps, verified by an impulse that leaves the body at exactly the configured cap.
- Implement a scene-gravity get and set API forwarding to the physics system, replacing the hardcoded gravity vector, verified by scripting zero gravity and observing a released body remain stationary.
- Add per-body sleep-threshold and allow-sleep controls mapped to the body and system sleep settings, verified by a never-sleep body staying active indefinitely under a tiny sustained force.
- Call the broad-phase optimization routine after a bulk static-body insertion batch so large static scenes get a balanced broad-phase tree, verified by a broad-phase quality assertion after adding thousands of static colliders.

## Raycasts & Scene Queries

- Route the raycast and raycast-all APIs through the Jolt narrow-phase cast-ray against real body shapes, replacing the current engine-geometry AABB and two-sphere approximation, so oriented boxes and true capsules are hit exactly, verified by a ray that misses a rotated box's AABB corner but correctly reports no hit on the real oriented shape.
- Add oriented shape casts by threading a rotation quaternion into the shape-cast and overlap queries instead of an identity basis, verified by a box-cast that only fits through a diagonal gap when rotated forty-five degrees.
- Return surface material, sub-shape id, and triangle or face index in the cast result from ray and shape casts so mesh-collider hits report which triangle and material was struck, verified by a ray into a two-material mesh reporting the correct material per face.
- Implement a single-target ray and shape cast that tests only one specified entity's body, verified by a ray that ignores an occluder and reports the intended body's hit.
- Implement a point-containment query that reports every body whose shape encloses a world point, verified by a point inside a box returning that box and a point just outside returning nothing.

## Joints & Constraints

- Implement a motorized and driven joint mode for the hinge and a new slider/prismatic constraint exposing target position, target velocity, and maximum motor force, verified by a hinge driven to and holding a commanded angle under gravity.
- Implement a spring and soft-constraint mode with frequency and damping on the distance constraint and a new spring joint type, verified by a suspended body oscillating at the configured frequency before settling.
- Implement a breakable-joint option with a break-force and break-torque threshold that removes the constraint and emits a break event when the applied impulse exceeds the limit, verified by a weight heavy enough to snap a fixed joint and fire the event.
- Implement a cone-twist swing-twist constraint with swing and twist angle limits for ragdoll-style joints, verified by a limb pinned so its swing stays within the configured cone.
- Implement a six-degrees-of-freedom configurable constraint exposing per-axis free, locked, and limited translation and rotation, verified by a body constrained to a one-axis rail with all other axes locked.
- Add a per-joint enable-collision-between-linked-bodies toggle, verified by two jointed bodies that either overlap freely or block each other according to the flag.
- Report live constraint reaction force and torque through the backend so gameplay can read joint stress, verified by a loaded joint reporting a reaction magnitude proportional to the hung mass.

## Contact & Trigger Events

- Include per-contact impulse magnitude and relative velocity in the collision event read from the accumulated manifold impulse so gameplay can scale impact effects, verified by a hard landing reporting a larger impulse than a gentle one.
- Emit all manifold contact points or an averaged contact patch in the collision event instead of only the first point, verified by a face-to-face box landing reporting multiple distinct contact points.
- Implement a contact-modification callback path that lets gameplay override per-contact friction and restitution or cancel a contact, verified by a one-way platform that passes upward but blocks downward via a cancelled contact.
- Enable sensor-versus-static and sensor-versus-kinematic detection so a trigger volume detects a static collider entering it, verified by a trigger firing enter against a moved static body.
- Emit body activation and deactivation wake and sleep events to script through a new pending-event channel, verified by a settling body producing exactly one sleep event.

## Character Controller

- Implement character crouch and shape-swap that live-switches the character shape with a penetration test before standing, verified by a character unable to stand under a low ceiling remaining crouched.
- Wire character-to-dynamic-body pushing by applying impulses to bodies the character reports as active contacts, verified by a walking character shoving a light dynamic crate along the floor.
- Implement character-versus-character collision by registering each character controller with the others, verified by two controllers unable to occupy the same space.
- Add stick-to-ground and walk-down-stairs handling so a descending character hugs downward slopes instead of launching off ledges, verified by a character keeping grounded state while walking down a staircase.
- Add a maximum-push-force limit and controlled slope-slide behavior so a character on a too-steep slope slides down at a controlled rate, verified by the steep-ground state producing downhill motion.

## Physics Materials

- Add friction-combine and restitution-combine modes (average, min, max, multiply) resolved between two contacting colliders via a contact callback, verified by a min-combine pair producing the lower of two friction values in the resulting deceleration.
- Implement a reusable physics-material asset carrying friction, restitution, combine modes, and a surface tag, referenced by colliders and loaded through the asset manager, verified by two colliders sharing one material asset that hot-reloads to change both.
- Implement per-triangle material assignment for mesh colliders via a material list on the mesh shape, verified by a raycast onto different triangles of one mesh returning different surface tags.

## Layers & Filtering

- Implement per-body ignore-pair collision filtering via a group and sub-group filter so two specific entities can ignore each other independent of layers, verified by two same-layer bodies passing through each other while still colliding with a third.

## Determinism, Networking & Async

- Implement physics state save and restore using Jolt's state serialization exposed as serialize and deserialize on the backend for rollback-netcode snapshots, verified by simulating, snapshotting, diverging, restoring, and reproducing bit-identical body poses.
- Add a deterministic-simulation mode that fixes worker-thread count and sorts body creation and iteration by stable entity id so runs reproduce across machines, verified by two runs with identical inputs producing identical final poses.
- Implement asynchronous physics stepping that launches the simulation update on the job system and reads results the following frame with interpolation so the main thread does not block on simulation, verified by main-thread step time dropping below the physics solve time under heavy body counts.

## Debug Visualization & Profiling

- Implement a Jolt debug-renderer backend that draws real simulated shapes, contact points, and constraint frames, replacing the hand-rolled wireframe approximations, verified by mesh and convex colliders rendering their actual triangles.
- Add velocity, sleeping-state, and center-of-mass overlays to physics debug draw, verified by a moving body showing a velocity vector that shrinks to zero and recolors when it sleeps.
- Expose per-step physics profiling stats (active body count, contact-constraint count, island count, solve time), verified by the active-body stat dropping as bodies fall asleep.

## High-Level Physics Systems

- Implement a wheeled-vehicle system backed by Jolt's vehicle constraint driven by throttle, brake, and steer inputs with per-wheel suspension and friction, verified by a four-wheel vehicle accelerating, steering, and settling on its suspension.
- Implement a ragdoll system that builds a Jolt ragdoll from a skeleton with per-bone shapes and swing-twist constraints and drives skinned-mesh bone transforms from the simulated pose, verified by a character collapsing into a stable, non-exploding ragdoll on death.
- Implement kinematic-to-ragdoll blending that pose-matches ragdoll bodies to an animated pose via motor-driven constraints, verified by a partially-driven ragdoll tracking an animation while reacting to an external shove.
- Implement buoyancy and water volumes that apply a buoyancy impulse with configurable fluid density and drag to bodies inside a marked region, verified by a low-density box floating at a stable waterline and a dense one sinking.
- Implement a soft-body and cloth system backed by Jolt's soft-body settings producing a simulated deformable mesh, verified by a pinned cloth draping over a sphere and coming to rest.
- Implement directional and radial force-field volumes for wind, explosions, and attractors that accumulate forces on overlapping bodies each fixed step via an overlap query plus force application, verified by an explosion field launching nearby dynamic bodies radially outward with distance falloff.

## 2D Physics (Box2D)

- Implement a Box2D-backed 2D physics scene system parallel to the Jolt path, mapping 2D rigidbody and collider components to Box2D bodies and fixtures and stepping a Box2D world, verified by a 2D box falling and resting on a 2D static ground segment.
- Implement 2D shape casts, overlaps, and contact-event routing for the Box2D backend mirroring the existing physics-backend query and event contract, verified by a 2D ray reporting the nearest fixture and a 2D trigger firing enter and exit.

# Capability 44 · Save, Prefab, Assets & Visual Scripting — Detailed Engineering Tasks

**Objective:** Close concrete persistence, prefab, asset-loading, and visual-scripting gaps with versioned data, asynchronous safety, conflict handling, editor integration, and end-to-end round-trip verification.

## Save System

- Implement an asynchronous save path that snapshots the save document into an immutable buffer on the calling thread and performs encode-plus-atomic-write on a background worker, returning a handle reporting pending, succeeded, or failed and invoking a completion callback, verified by a test that mutates the source immediately after issuing the save and confirms the written bytes match the pre-mutation snapshot.
- Build a save-slot manager that stores each save as a slot directory under a configurable root, enumerable and creatable and deletable by slot id, verified by a test that creates three slots, lists them, deletes one, and confirms the remaining two enumerate correctly.
- Add a per-slot metadata header written and readable without decoding the full payload, carrying at least save timestamp, accumulated playtime, a caller-supplied label, schema version, and an optional thumbnail blob, verified by a test that reads the metadata of a large save without materializing its entry table.
- Implement an autosave and checkpoint scheduler that maintains a fixed-size ring of rotating autosave slots triggered on an interval or an explicit checkpoint event and evicts the oldest when full, verified by a test that fires more autosaves than the ring size and confirms exactly the most recent survive in newest-first order.
- Implement a world-state-to-save bridge that captures a registered set of component types from selected live entities into a save document with stable per-entity save keys and restores them back into a scene, verified by a round-trip test that saves a populated scene, clears it, restores, and confirms component values and entity relationships match.
- Add an optional compression stage that compresses the entry payload behind a format flag and transparently decompresses on load, verified by a test that a large save is smaller than its uncompressed encoding and round-trips to an identical entry table.
- Add optional authenticated encryption of the payload with a per-save nonce and a tamper-detecting authentication tag behind a format flag, rejecting a modified file with a distinct authentication-failure status, verified by a test that flips one payload byte and confirms the load is rejected rather than silently decoded.
- Append an integrity checksum of the payload to the file and verify it on load, surfacing a corrupt-checksum status distinct from structural corruption, verified by a test that corrupts a byte and confirms the checksum-specific status.
- Extend the save value type into a recursive form that can hold ordered arrays and nested string-keyed maps in addition to the existing scalars, with a container tag and typed accessors, verified by a round-trip test that stores a nested object containing an array and reads every leaf back with correct types.
- Add a binary-blob save value type for opaque payloads such as thumbnails and replay chunks with an enforced size bound and round-trip serialization, verified by a test that stores and reloads a multi-kilobyte blob unchanged.
- Implement a keep-previous backup on overwrite that renames the prior file to a backup sibling on a successful new atomic write and transparently falls back to the backup when the primary fails verification on load, verified by a test that corrupts the primary and confirms the backup is loaded.
- Add a non-loading save-inspection API that reports a file's magic validity, schema version, domain, and the exact migration chain that would run to reach the current version, verified by a test against an older-version file that lists the pending migration steps without decoding the payload.

## Prefab System

- Implement nested-prefab preservation at capture time so that when capture traverses a subtree that is itself a live instance of another registered prefab, it records that subtree as a nested-prefab reference plus its local overrides instead of flattening its nodes, verified by a test that captures a hierarchy containing an existing instance and confirms the resulting prefab holds a nested reference rather than duplicated nodes.
- Build a reverse prefab-usage index that answers which scenes and which other prefabs reference a given prefab (including as a nested prefab), kept current as prefabs and scenes are registered and unregistered, verified by a test that registers a prefab used as a nested reference in two others and confirms both are reported as dependents.
- Implement three-way conflict reporting on instance refresh and apply-override that, when a source property changed and the instance also overrides that same property, emits a conflict record carrying the base, source-new, and instance values instead of silently keeping one, verified by a test that changes a property on both source and instance and confirms a conflict is reported.
- Add a removed-component override that represents a component present on the source node but deleted on an instance as a first-class override distinct from removed-object, serialized to the prefab asset and both revertible and applyable, verified by a round-trip test that removes a component on an instance, reloads, and confirms the removal is preserved and can be reverted.
- Add an added-component override symmetrical to the existing added-child that represents a component added on an instance whose source node lacks it, tracked in the override report and both applyable-to-source and revertible, verified by a round-trip test.
- Implement a sibling-order override that records a child reordered to a new index among its siblings within an instance and re-applies that ordering on instantiation and refresh, verified by a test that reorders two children on an instance, reloads, and confirms the order is preserved.
- Add an apply-override variant that targets a chosen layer in the variant chain rather than a single asset path, verified by a test that applies an instance override to a mid-chain variant and confirms deeper variants and instances inherit it while the root template is unchanged.
- Implement promotion of an instance's current override set into a new variant asset that bakes the instance divergence as the variant's override list against its source prefab, verified by a test that promotes an instance and confirms instantiating the new variant reproduces the instance's values.
- Implement a serializable structural prefab diff between two prefab versions or a prefab and an instance that reports added, removed, and reparented nodes plus changed properties in a stable machine-readable form, verified by a test that diffs a known before-and-after pair and matches the expected delta.
- Implement cascade refresh through nested-prefab boundaries so that when a base prefab changes, instances that embed it as a nested prefab are re-baked with their nested overrides re-applied, verified by a test that edits a base prefab and confirms an instance embedding it via a nested reference reflects the change while retaining its nested overrides.

## Asset System

- Implement asynchronous asset loading backed by a worker queue where a load request returns immediately with a handle transitioning through loading to loaded or failed and the payload is produced off the main thread, verified by a test that issues many loads, pumps completion, and confirms all payloads become available without a main-thread disk read.
- Add per-request load priority and cancellation to the async queue so higher-priority requests are serviced first and a cancelled request never produces a payload, verified by a test that enqueues mixed-priority requests and confirms completion order follows priority and a cancelled id is dropped.
- Implement a resident-memory budget with least-recently-used eviction that tracks each cached payload's byte cost and evicts unreferenced assets when a configurable budget is exceeded, verified by a test that loads past the budget and confirms the coldest unreferenced asset is evicted while referenced ones stay resident.
- Implement content-hash payload deduplication so two registered assets with identical content hash and type share a single cached payload instance rather than being decoded twice, verified by a test that loads two duplicate-content assets and confirms one loader invocation and one shared payload pointer.
- Add a reverse-dependency index to the registry built from each asset's dependencies and maintained on upsert, remove, and clear, exposing a constant-time query of assets that depend on a given asset, verified by a test that mutates dependencies and confirms the dependents query stays correct.
- Implement topological dependency-first load ordering that, given a root asset, produces and executes a load order in which every dependency is resident before its dependents and reports a diagnostic on a dependency cycle, verified by a test over a diamond dependency graph confirming ordering and over a cyclic graph confirming a diagnosed failure.
- Implement a hot-reload file watcher that monitors mounted physical paths, detects content changes via the content hash, reloads the changed payload in place, and notifies live holders through a reload callback and cache generation bump, verified by a test that rewrites a watched file and confirms holders observe the new payload.
- Implement hot-reload propagation that, when a reloaded asset's content changes, transitively invalidates and reloads its dependents via the dependents index, verified by a test that edits a leaf asset and confirms a dependent asset two levels up is reloaded.
- Implement a path-independent stable asset identifier generated once, persisted in a sidecar metadata file, and read back on discovery so the asset id no longer derives from the virtual path and survives a move or rename, verified by a test that moves an asset on disk and confirms its id is unchanged and all references still resolve.
- Add identifier-collision detection on discovery and import that reports a diagnostic when two assets resolve to the same identifier instead of silently overwriting the registry entry, verified by a test that forces a collision and confirms the diagnostic and that neither asset is lost.
- Implement a batch preloader that loads a named manifest of assets asynchronously while reporting progress as loaded-over-total count and bytes for a loading screen, verified by a test that preloads a manifest and confirms progress advances monotonically to completion and every asset ends resident.

## Visual Scripting Graph

- Add variable-get and variable-set node kinds with matching intermediate-representation opcodes that read and write a declared graph variable by name through the execution context, type-checked against the variable's declared type at compile time, verified by a round-trip test that sets a variable and a downstream get reads the same value.
- Implement persistent per-instance variable storage that survives across lifecycle ticks rather than being cleared each pass and is serialized with the behaviour instance, verified by a test that increments a variable across multiple ticks and confirms accumulation and correct restore after serialization.
- Add loop nodes — a counted for-loop with an index output, a while-loop with condition and completed pins, and a for-each over a collection — each with a bounded-iteration watchdog, verified by a test that a loop body executes exactly the expected number of times and a runaway loop is stopped with a diagnostic.
- Add pure math nodes (add, subtract, multiply, divide, modulo, negate, min, max, clamp) for the integer and floating-point value types that expose a value output with no execution pin, verified by a test evaluating each against known operands.
- Add logic and comparison nodes (and, or, not, xor, equal, not-equal, less, greater, less-equal, greater-equal) producing a boolean output, verified by a truth-table test covering each node.
- Implement a wait and delay latent node that suspends the execution flow for a duration, frame count, or until a condition and resumes on a later tick, persisting its pending timer state so a save or load during the wait resumes correctly, verified by a test that confirms resumption after the specified ticks and after a serialize-deserialize mid-wait.
- Implement user-defined function and subgraph nodes with typed input and output parameters and a local body, compiled once into a reusable intermediate-representation function and invoked from multiple call sites with an argument frame, verified by a test that calls a function with arguments from two sites and confirms each returns the correct result.
- Add node canvas layout data (position on every node plus size and color for comment nodes) serialized in the graph asset, along with reroute nodes on data edges, so an editor can round-trip a graph's visual layout losslessly, verified by a test that saves and reloads a laid-out graph and confirms positions are stable.
- Implement live graph debugging with per-node breakpoints, single-step, and pin-value watches that pause execution at a breakpoint and expose the stored pin values to an attached debugger, verified by a test that sets a breakpoint, runs, and confirms execution halts at the node and the expected pin values are readable.
- Implement an execution-trace recorder that captures which nodes and edges fired during a pass with per-node timing for editor highlighting of the active path, verified by a test that runs a branching graph and confirms only the taken path is recorded.
- Build a node-authoring SDK that lets external code register a new node kind with custom pins, a compile hook that emits intermediate representation, and a runtime execute hook, so gameplay code can add first-class control-flow or data nodes beyond the fixed built-in set, verified by a test that registers a custom node, compiles a graph using it, and executes it to the expected output.
- Add an editor-facing graph mutation and validation API that adds and removes nodes, connects and disconnects pins with live type-checking that rejects incompatible connections and execution cycles, and supports undo and redo, verified by a test that an incompatible connection is rejected and a valid edit is undoable to the prior graph state.
- Add a collection and array runtime value type and the nodes to operate on it (make-array, length, get, set, append, contains), verified by a test that builds an array, appends, and reads elements back with correct types.
- Add explicit type-cast nodes and compile-time implicit numeric widening for pin connections so numeric conversions are well-defined rather than rejected or silently truncated, verified by a test that connects an integer output to a floating-point input and confirms the widened value at runtime.
