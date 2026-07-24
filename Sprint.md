# 1 · Foundation / Core

## Core / Foundation

- [ ] Add a shared `core` base module every subsystem depends on
- [ ] Add an open-addressing hash map and hash set (swiss / robin-hood)
- [ ] Add a small-buffer-optimized vector (SmallVector / InlineVector)
- [ ] Add fixed-capacity and stack-backed array/vector types
- [ ] Add intrusive linked list and intrusive hash map
- [ ] Add a custom string type with small-string optimization
- [ ] Add an interned string / string-ID table with reverse lookup
- [ ] Add a shared hashing header (FNV-1a, xxHash, hash_combine, byte hashing)
- [ ] Replace ad-hoc per-file hashing with the shared hashing header
- [ ] Add a compile-time stable `TypeId<T>()` facility
- [ ] Add a generic generational handle / slot-map template
- [ ] Add a generic `Result<T, E>` / `Expected` and error-category system
- [ ] Add engine-wide `Span` and `StringView` types
- [ ] Add bit utilities (bitset, enum flags, popcount/clz wrappers)
- [ ] Add UUID / GUID generation and parsing
- [ ] Add a tagged-variant helper with visitation

## Logging / Diagnostics / Assertions

- [ ] Add an engine-wide logging facility with levels and categories
- [ ] Add pluggable log sinks (console, file, editor console, debugger)
- [ ] Add async logging with rotation and unified formatting
- [ ] Add structured logging with key/value fields
- [ ] Add per-category runtime verbosity thresholds
- [ ] Add assertion macros (assert/check/ensure/verify) that are build-config aware
- [ ] Add a fatal-error handler with message and safe shutdown
- [ ] Add stack-trace capture with symbol resolution
- [ ] Add crash minidump writing for runtime, CLI, and hub
- [ ] Add a cross-platform crash handler
- [ ] Add a runtime in-game console with command entry
- [ ] Route all subsystem diagnostics through the unified logger

## Memory Management

- [ ] Add a linear / arena (bump) allocator
- [ ] Add a stack allocator with scoped markers
- [ ] Add a pool / block allocator for fixed-size objects
- [ ] Add a per-frame double-buffered frame allocator
- [ ] Add a general heap allocator wrapper with alignment support
- [ ] Add alignment helpers (AlignUp/AlignDown, aligned alloc/free)
- [ ] Add a memory tracker with tagged allocation categories
- [ ] Add per-subsystem memory budgets with over-budget diagnostics
- [ ] Add allocation statistics and telemetry (peak, live, count per tag)
- [ ] Add leak detection for debug builds
- [ ] Add STL allocator adapters that route through the tracker
- [ ] Add an intrusive ref-counted base and custom smart pointers
- [ ] Add a slot-map allocator with generational handle safety
- [ ] Route third-party allocators through the memory tracker

## Math / Geometry

- [ ] Add SIMD-accelerated Vec3/Vec4/Quat/Mat4 with scalar fallback
- [ ] Add a Transform type with compose, inverse, and interpolation
- [ ] Add Mat4 general inverse, TRS decompose, and orthonormalize
- [ ] Add perspective, orthographic, lookAt, and frustum matrix builders
- [ ] Add Sphere, OBB, Frustum, Capsule, Triangle, Segment, and Circle primitives
- [ ] Add ray intersection tests: AABB, Sphere, Triangle, OBB
- [ ] Add volume overlap tests: AABB-AABB, Sphere-Sphere, Frustum-AABB, Frustum-Sphere
- [ ] Add closest-point and distance queries (point-segment, point-AABB, segment-segment)
- [ ] Add spline types: Bezier, Catmull-Rom, Hermite, B-spline
- [ ] Add arc-length parameterization and spline path sampling
- [ ] Add an optional double-precision math variant for large worlds
- [ ] Add half-float (float16) conversion helpers
- [ ] Add swizzles and component-wise ops across Vec2/Vec3/Vec4
- [ ] Move duplicated renderer/editor matrix helpers onto the shared math library
- [ ] Move frustum/sphere culling primitives into the shared math library

## CPU / Threading / Job System

- [ ] Extract a general-purpose job system decoupled from ECS
- [ ] Add a lock-free work-stealing deque (Chase-Lev)
- [ ] Add fiber-based job execution with context switching
- [ ] Add job priorities and per-job continuations at the execution layer
- [ ] Add lock-free MPMC and SPSC queues
- [ ] Add a lock-free ring buffer
- [ ] Add a main-thread / render-thread dispatch queue
- [ ] Add an async task/future framework (task, when_all, when_any)
- [ ] Add parallel-for over arbitrary ranges outside ECS chunks
- [ ] Add an async I/O framework for asset and file loading
- [ ] Add thread naming and per-thread scratch context
- [ ] Implement the AVX2 and AVX-512 kernel backends
- [ ] Add a general SIMD kernel library reusable outside ECS transforms
- [ ] Add a cross-subsystem task-barrier abstraction

## Platform Abstraction Layer

- [ ] Add a central platform (HAL) module abstracting OS specifics
- [ ] Add a windowing abstraction with pluggable backends
- [ ] Add Linux (X11 / Wayland) window and input backends
- [ ] Add macOS window and input backends
- [ ] Add Android window, input, and lifecycle backends
- [ ] Add a virtual filesystem with mount points and archive/pak support
- [ ] Add async file I/O to the virtual filesystem
- [ ] Add a high-resolution clock and frame-timer abstraction
- [ ] Add an environment/paths service (exe, user data, config, cache, temp)
- [ ] Add a file/directory watcher (ReadDirectoryChanges / inotify / FSEvents)
- [ ] Add a subprocess spawning and pipe abstraction
- [ ] Add a CPU/platform info service (cores, cache line, page size, features)
- [ ] Promote dynamic library loading out of the module loader into the HAL
- [ ] Add a thread wrapper with naming, priority, and affinity
- [ ] Add power/battery and display-info queries for scalability

## Reflection / Type System / Metadata

- [ ] Add a global type registry for arbitrary C++ types, not just ECS components
- [ ] Add content-stable type IDs independent of registration order
- [ ] Add enum reflection with name-value tables
- [ ] Add reflection for nested structs and composite fields
- [ ] Add string, array, map, and handle/reference field types to reflection
- [ ] Add method/function reflection for native types
- [ ] Add attribute metadata (range, tooltip, category, transient, hidden)
- [ ] Add a codegen path to remove hand-written field registration
- [ ] Drive the editor inspector UI automatically from reflection metadata
- [ ] Drive serialization automatically from reflection metadata
- [ ] Add reflection-based property binding for scripting
- [ ] Add default-value and validation metadata per field

## Serialization

- [ ] Add a unified archive abstraction shared by all subsystems
- [ ] Add a JSON serialization backend for data and settings
- [ ] Add a diff-friendly, hand-editable text scene/prefab/resource format
- [ ] Add endian-portable binary serialization across all codecs
- [ ] Add schema evolution (tolerant add/remove/rename of fields)
- [ ] Extend the declarative migration framework beyond save games to all formats
- [ ] Add object-graph serialization with pointer/reference fixup
- [ ] Add cross-asset and cross-entity reference field serialization
- [ ] Extend the value model to strings, arrays, maps, nested structs, and handles
- [ ] Add reflection-driven automatic serialization for reflected types
- [ ] Add versioned per-component schema headers with upgrade hooks
- [ ] Add binary-to-text round-trip conversion for debugging

## Config / CVars / Settings

- [ ] Add a typed CVar registry of named runtime variables
- [ ] Add CVar get/set through the runtime console
- [ ] Add CVar overrides from command line, environment, and config file
- [ ] Add a hand-editable, diffable text config format (engine, user, project)
- [ ] Add layered settings tiers (engine, project, user, platform, command line)
- [ ] Add per-platform config overrides
- [ ] Add hot-reload of settings via the file watcher
- [ ] Add a unified command-line parser shared by all executables
- [ ] Add a `-set key=value` override pipeline into CVars/settings
- [ ] Add settings schema, validation, and migration for text config
- [ ] Auto-build the settings UI from reflection metadata
- [ ] Add user-preferences and editor-layout persistence

## Module / Plugin System

- [ ] Add external plugin manifest files (name, version, dependencies, content)
- [ ] Add plugin discovery/scan of plugin directories
- [ ] Add semver version constraints and compatibility checking
- [ ] Add optional vs required dependencies with failure isolation
- [ ] Add automatic hot-reload driven by a filesystem watcher
- [ ] Add an inter-module typed service/interface registry
- [ ] Harden the plugin ABI to a versioned struct-of-function-pointers boundary
- [ ] Add per-plugin content/asset mounting through the virtual filesystem
- [ ] Add an editor "reload plugin" action with state preservation
- [ ] Add plugin load-failure sandboxing so one plugin cannot crash the host

# 2 · GPU

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

## Production-grade renderer architecture

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

## GPU-driven render pipeline

- [ ] Create GPU-visible instance, bounds, meshlet, LOD, and draw-command buffers. (partial: GPU-visible bounds + packed metadata exist; meshlet-geometry and indirect draw-command buffers still missing)
- [ ] Build hierarchical Z buffer inputs from the current depth buffer.
- [ ] Dispatch HZB seed and downsample compute passes.
- [ ] Dispatch instance occlusion culling against HZB.
- [ ] Compact visible instances into a GPU-visible draw list.
- [ ] Generate indirect draw arguments on GPU.
- [ ] Submit indirect draws from generated draw arguments.
- [ ] Dispatch meshlet or cluster culling for GPU-driven meshes.
- [ ] Cull meshlets by frustum, cone backface, and occlusion data.
- [ ] Select LOD on GPU using screen coverage and per-mesh thresholds.
- [ ] Keep CPU LOD selection as validation and fallback.
- [ ] Expose GPU culling, indirect draw, meshlet, and LOD counters in submit stats.
- [ ] Add GPU culling debug readback for visible instance counts.

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

## GPU HDR luminance histogram and temporal auto-exposure

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
- [ ] Add tonemap controls for shoulder, toe, saturation, contrast, white point, and per-scene override profiles.
- [ ] Add tonemapping calibration scenes and golden screenshots for ACES, neutral, and custom curves.
- [ ] Add bloom threshold, knee, radius, and mip-chain debug visualization.
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

## Production PBR materials

- [ ] Add a renderer-visible unsupported-material-feature bitset for clearcoat, sheen, transmission, subsurface, anisotropy, decals, layers, and future extensions. (partial: per-feature ParsedButIgnored classification + warnings exist; single packed bitset type still missing)
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

## Highest-quality milestones

- [ ] Implement GPU HDR luminance histogram and temporal auto-exposure first to close the real HDR/post-process pipeline.
- [ ] Stabilize renderer capability matrix and backend quality tiers before adding more backend-specific features.
- [ ] Build the render graph/resource lifetime foundation first: barriers, aliases, transient buffers, pass markers, and per-pass profiling.
- [ ] Add shader permutation system and material compiler before expanding advanced PBR, terrain, vegetation, and skinning variants.
- [ ] Add visual debug views early so every new renderer feature can be inspected without RenderDoc.
- [ ] Add golden screenshots and GPU smoke harness per backend/quality tier before tuning lighting and post-process.
- [ ] Add performance budgets and profiler JSON before shipping large systems such as terrain, clustered lighting, GI, and streaming.
- [ ] Implement dynamic resolution and quality scaler after profiler metrics are trustworthy.
- [ ] Expand production features in this order: lighting/shadows, IBL/probes, post-process/HDR, terrain/vegetation, skinning/animation, GI.

# 3 · Runtime Gameplay / World

## Scene / Entity System

- [ ] Add named update phases (early, simulation, late, presentation) with a dedicated fixed-timestep phase
- [ ] Add system groups/sets with nested ordering and shared enable/disable
- [ ] Add runtime enable/disable of individual systems
- [ ] Add one-shot / run-once systems
- [ ] Add per-system persistent local state
- [ ] Add explicit sync-point insertion between phases
- [ ] Add multi-scene loading with additive and single-scene modes
- [ ] Add asynchronous scene loading with progress and completion events
- [ ] Add scene unloading with safe reference and dependency handling
- [ ] Add persistent entities that survive non-additive scene loads
- [ ] Add cross-scene entity references resolved on load
- [ ] Add sub-scene and nested-scene composition
- [ ] Add scene metadata (bounds, entity count, dependencies, thumbnail)
- [ ] Add scene bake and content preprocessing on save
- [ ] Add stable global entity identifiers for references and networking
- [ ] Add an entity reference/handle type safe across destroy and reload
- [ ] Add per-entity gameplay layers with camera culling masks
- [ ] Add sorting layers and in-layer order for overlay and 2D content
- [ ] Add exclusive entity relationships with cardinality constraints
- [ ] Add on-delete cleanup policies for relationships (cascade, orphan, block)
- [ ] Add relationship-traversal query terms (ancestors, descendants, cascade)
- [ ] Add wildcard and pair queries over relationships
- [ ] Unify typed tags and string tags behind one query-by-tag surface
- [ ] Add transform constraints (parent, aim, look-at, position/rotation lock)
- [ ] Add a spatial query acceleration structure for gameplay (nearest, overlap, ray)
- [ ] Add an entity selection and picking API for gameplay code
- [ ] Add entity groups / streaming cells addressable as sub-worlds
- [ ] Add world partitioning with load/unload of entity regions
- [ ] Add entity archetype templates distinct from prefabs
- [ ] Add bulk entity spawn/despawn with pooling
- [ ] Add entity iteration snapshots stable across structural change
- [ ] Add deterministic entity id remapping across save/load and streaming

## Component System

- [ ] Add singleton / world-resource components with get/set API
- [ ] Add cheap per-component enable/disable without archetype moves
- [ ] Add required-component declarations that auto-add dependencies
- [ ] Add component add/remove conflict validation from declared dependencies
- [ ] Add dynamic-buffer components (growable per-entity arrays)
- [ ] Add immutable shared blob data referenced by components
- [ ] Add managed component storage for reference-type payloads
- [ ] Add component pooling for churn-heavy component types
- [ ] Add chunk-level iteration and chunk-component metadata access
- [ ] Add reactive/monitor systems triggered by component changes
- [ ] Add per-field change events distinct from whole-component modified
- [ ] Add separate on-set and on-remove value semantics for observers
- [ ] Add shared-value component grouping for draw/update batching
- [ ] Add component lifecycle hooks (construct, destruct, copy, move)
- [ ] Add per-archetype component default-value templates
- [ ] Add scoped component borrow guards surfaced to gameplay code
- [ ] Add per-component-type serialization version and upgrade hooks
- [ ] Add component migration when a type layout changes
- [ ] Add component change versioning queryable from scripts

## Prefab / Instancing

- [ ] Add first-class removed-component overrides on instances
- [ ] Add per-property override lock/protection on instances
- [ ] Add configurable propagation policy for base component add/remove
- [ ] Add three-way merge and conflict reporting for concurrent asset edits
- [ ] Add nested-instance depth diagnostics and configurable limits
- [ ] Add selective refresh that preserves overridden fields
- [ ] Add an instance-to-asset apply-selected-changes workflow
- [ ] Add runtime prefab creation and saving from live entities
- [ ] Add a prefab dependency graph with reference integrity checks
- [ ] Add prefab variant diff visualization
- [ ] Add asynchronous streamed instantiation for large prefab batches
- [ ] Add prefab preview thumbnails generated on capture

## Gameplay Components / Library

- [ ] Expand the component-authoring library with reusable gameplay building blocks
- [ ] Add first-class component composition and dependency helpers to the library
- [ ] Add a component-authoring wizard and code-template generator
- [ ] Add a movement component with configurable kinematics
- [ ] Add a kinematic character-controller component with slopes, steps, and crouch
- [ ] Add steering-behavior components (seek, flee, wander, pursue, arrive)
- [ ] Add a waypoint and patrol-route component
- [ ] Add a spline-follow / path-follow component
- [ ] Add a targeting and lock-on component
- [ ] Add an attributes component set (health, stamina, custom stats) with clamping and regeneration
- [ ] Add a component-driven damage and health pipeline with mitigation and death events
- [ ] Add a stat-modifier and buff/debuff component driven by data
- [ ] Add a data-driven skill and cooldown component set
- [ ] Add a team / faction component with relationship queries (friendly, hostile, neutral)
- [ ] Add a projectile component with lifetime, homing, and impact events
- [ ] Add a weapon/emitter component with fire rate and spread
- [ ] Add a damage-zone / hazard component
- [ ] Add trigger-volume and overlap components with enter/stay/exit events
- [ ] Add a sensor/perception component (sight, hearing, proximity)
- [ ] Add an interaction component set (focus, prompt, activate)
- [ ] Add a door / switch / lever interactable component set
- [ ] Add a grab / carry / throw component set
- [ ] Add spawn-point and spawner components
- [ ] Add respawn handling as reusable components
- [ ] Add object pooling for frequently spawned entities
- [ ] Add an inventory and item-instance component model
- [ ] Add pickup, equip, and drop components
- [ ] Add a pickup-magnet and auto-collect component
- [ ] Add a tween / property-animation component
- [ ] Add a timeline/sequence player component for sequenced actions
- [ ] Add audio-trigger and one-shot sound components
- [ ] Add a footstep / surface-response component
- [ ] Add a ragdoll toggle and physics-blend component
- [ ] Add a world-space UI-attachment component (health bar, nameplate)
- [ ] Add a virtual-camera component with blends and priorities
- [ ] Add a checkpoint and level-progression component set
- [ ] Add an objective and quest tracking component set
- [ ] Add reusable local-multiplayer components for multiple local players
- [ ] Add ready-to-use gameplay component sample packages that ship with the engine

## Scripting

- [ ] Add a managed C# / .NET scripting backend
- [ ] Add an optional Python scripting backend
- [ ] Make the scripting backend registry extensible beyond the fixed set
- [ ] Add an engine-integrated coroutine scheduler with wait-for-seconds, frames, and conditions
- [ ] Add yield-aware behaviours that can suspend inside their own body
- [ ] Add async/await style operations bound to the coroutine scheduler
- [ ] Add editor-exposed property attributes (range, tooltip, header, hidden)
- [ ] Add script execution-order configuration per behaviour type
- [ ] Add script-to-script references resolved through the entity/component model
- [ ] Add serialization of script instance state for save and hot-reload
- [ ] Add a standard debug-adapter server for external IDE debugging
- [ ] Extend step debugging to the native and visual scripting backends
- [ ] Support per-instance exposed variables across all backends
- [ ] Add sandbox resource limits (instruction budget, memory ceiling, execution watchdog)
- [ ] Add per-script profiling hooks and memory accounting
- [ ] Add compile and runtime error surfacing in the editor with source locations
- [ ] Add hot-reload that preserves script instance state
- [ ] Add live tuning of exposed variables while playing
- [ ] Add a script package/module import system with versioning
- [ ] Add a scripting API versioning and deprecation policy
- [ ] Add typed auto-generated bindings from reflection metadata
- [ ] Add script templates and creation wizards
- [ ] Add a script unit-test harness and headless run mode

## Visual Scripting

- [ ] Add a node-graph authoring canvas in the editor
- [ ] Add loop nodes (for, while, for-each) with iteration guards
- [ ] Add user-defined functions, macros, and collapsed subgraphs
- [ ] Add a reusable subgraph and graph-template library
- [ ] Add a custom-node authoring SDK
- [ ] Add first-class variable get and set nodes with graph-local and shared scopes
- [ ] Add math, logic, comparison, and operator nodes
- [ ] Add literal and constant nodes
- [ ] Add type-conversion, cast, and enum/flags nodes
- [ ] Add array and map container nodes
- [ ] Add delay and latent action nodes
- [ ] Add timeline and sequence nodes
- [ ] Add a state-machine node construct with persisted state
- [ ] Add event nodes bound to the messaging system
- [ ] Add interop nodes to call and be called by text scripts
- [ ] Add live graph debugging with wire-value inspection and breakpoints
- [ ] Add step-through execution and per-node profiling
- [ ] Add node and graph versioning with migration
- [ ] Add graph-to-native compilation parity tests
- [ ] Add graph unit tests and golden-execution fixtures
- [ ] Add node search, comments, and reroute nodes for authoring ergonomics

## Event / Messaging System

- [ ] Add an engine-wide native event bus independent of the scripting runtime
- [ ] Add compile-time typed channels without string keys or value boxing
- [ ] Add subscriber priority and deterministic delivery ordering
- [ ] Add scoped RAII connection handles with automatic disconnect
- [ ] Add one-shot and auto-expiring subscriptions
- [ ] Add weak subscriptions that drop when the owner is destroyed
- [ ] Add serializable event assets bindable in the inspector
- [ ] Add local request/response (query) messaging distinct from fire-and-forget
- [ ] Add event nodes for the visual scripting backend
- [ ] Add queued events with per-channel frame draining
- [ ] Add event batching and aggregation per frame
- [ ] Add debounce and throttle policies per channel
- [ ] Add per-recipient routing filters (tag, layer, team, session)
- [ ] Add event recording, replay, and an inspectable event log
- [ ] Add a live event monitor panel in the editor
- [ ] Add back-pressure and overflow diagnostics for deferred queues

## Time / Tick / Update Loop

- [ ] Add real coroutines for scripts and native code
- [ ] Add wait-for-seconds, wait-for-frames, wait-for-event, and wait-for-load yields
- [ ] Add script-facing task creation, cancellation, and chaining
- [ ] Add a frame-pacing governor with target framerate and vsync control
- [ ] Add named user-defined tick groups and stage insertion points
- [ ] Add per-system time budgets with overrun detection
- [ ] Add adaptive tick rate for background and off-screen systems
- [ ] Add per-entity and per-layer independent clocks
- [ ] Add time-dilation zones and localized slow-motion
- [ ] Add a global pause that distinguishes gameplay, physics, and UI time
- [ ] Add async asset and scene loading with progress and yields
- [ ] Add a timeline/sequence player for scripted gameplay moments
- [ ] Add slow-frame and hitch detection with attribution
- [ ] Add a deterministic time source for replay and networking
- [ ] Add deterministic fixed-step replay of the whole simulation
- [ ] Add catch-up and interpolation controls exposed to gameplay
- [ ] Add a tick-group profiler surfaced in the editor

## Save / Load / Persistence

- [ ] Bridge live world and entity state into the save system
- [ ] Add a per-scene persistence scope and object registry
- [ ] Add a selective persistence policy per component and per entity
- [ ] Add save slots with enumeration and per-slot metadata
- [ ] Add metadata capture (name, playtime, timestamp, level, thumbnail)
- [ ] Add save thumbnails captured from the active view
- [ ] Add an autosave and checkpoint system with rotation
- [ ] Add incremental and differential saves between checkpoints
- [ ] Add asynchronous background save and load
- [ ] Add save compression
- [ ] Add tamper detection and optional encryption
- [ ] Add nested and structured save values (arrays, maps, blobs)
- [ ] Add versioned migration for world-state saves
- [ ] Add save-corruption recovery with backup slots
- [ ] Add platform cloud-save integration behind an abstraction
- [ ] Add partial and streamed saves for large worlds
- [ ] Add a save-data schema browser and inspector in the editor
- [ ] Add save/load integrity tests across version migrations

## Data-Driven Design

- [ ] Add a data-table asset with typed row structs and keyed lookup
- [ ] Add spreadsheet and CSV import into typed data tables
- [ ] Add standalone curve and gradient assets referenceable across content
- [ ] Add a shared config/data asset base for designer-authored constants
- [ ] Add enum and flag definition assets shared across content
- [ ] Add gameplay-data registries (items, abilities, stats, loot) keyed by id
- [ ] Add data inheritance and composition (base rows with overrides)
- [ ] Add cross-table references with foreign-key integrity
- [ ] Add expression/formula assets evaluated at runtime
- [ ] Add named tunables with runtime hot-reload
- [ ] Add validation and referential-integrity checks across data assets
- [ ] Add a query and filter API over data tables
- [ ] Add data baking into fast runtime lookup structures
- [ ] Add a balancing/tuning dashboard over data tables
- [ ] Add runtime-authored and player-editable data support
- [ ] Add live data-asset hot-reload into running gameplay
- [ ] Add export and diff tooling for data tables

## State Machines / Behavior / AI Logic

- [ ] Add a reusable finite state machine with states, transitions, and guards
- [ ] Add on-enter, on-update, and on-exit callbacks per state
- [ ] Add event-driven and condition-driven transitions
- [ ] Add any-state and global transitions
- [ ] Add a hierarchical state machine with nested and parallel state regions
- [ ] Add history states that resume the last active sub-state
- [ ] Add a pushdown state stack for layered states
- [ ] Add per-state timers, durations, and timeouts
- [ ] Add a visual state-machine editor
- [ ] Add serialization and visualization of active states and transitions
- [ ] Add a behavior-tree library with composite, decorator, and leaf nodes
- [ ] Add behavior-tree services and a reusable decorator library
- [ ] Add behavior-tree subtrees and shared tree assets
- [ ] Add behavior-tree live debugging and visualization
- [ ] Add a per-agent blackboard with typed keys
- [ ] Add blackboard synchronization across agents and scopes
- [ ] Add a utility-based decision system with authored utility curves
- [ ] Add a goal-oriented action planner
- [ ] Add sensor/perception feeding into decision systems
- [ ] Add cooldown and global-cooldown utility types
- [ ] Add a timer wheel for large numbers of concurrent timers
- [ ] Add a stateful visual-scripting state machine backed by the same runtime
- [ ] Add serialization of behavior-tree and planner runtime state

# 4 · Terrain Editor

## Terrain Data Model

- [ ] Add a heightfield terrain resource with configurable resolution and world size
- [ ] Add a tiled/sector terrain layout for arbitrarily large terrains
- [ ] Add multi-resolution height storage with seamless tile boundaries
- [ ] Add a tile LOD hierarchy with stitched edges
- [ ] Add double-precision world coordinates for large-world terrains
- [ ] Add a signed-height and world-space coordinate mapping
- [ ] Add per-vertex and per-tile bounds with fast dirty tracking
- [ ] Add fine-grained dirty-region tracking for partial recompute
- [ ] Add a terrain hole mask for caves, tunnels, and cutouts
- [ ] Add weight/splat layers for material blending
- [ ] Add a scatter/instance layer for placed props and vegetation
- [ ] Add a spline layer for roads, rivers, and paths
- [ ] Add a metadata layer (biome id, navigation flags, physics material)
- [ ] Add per-tile compression for height, weights, and masks
- [ ] Add per-tile checksums for integrity and change detection
- [ ] Add a versioned terrain asset format with migration
- [ ] Add copy, crop, resize, and re-origin operations on terrain assets
- [ ] Add merge and split operations for terrain tiles
- [ ] Add an undo-history data model scoped to tiles and layers
- [ ] Add serialization that stores only modified tiles

## Non-Destructive Layer Stack

- [ ] Add an adjustment-layer stack applied non-destructively over the base height
- [ ] Add sculpt layers as stack entries
- [ ] Add noise layers as stack entries
- [ ] Add erosion layers as stack entries
- [ ] Add stamp layers as stack entries
- [ ] Add spline layers as stack entries
- [ ] Add per-layer masks that limit where a layer takes effect
- [ ] Add mask editing (paint, fill, invert, blur) per layer
- [ ] Add layer blend modes (add, subtract, min, max, replace, average)
- [ ] Add per-layer opacity and strength
- [ ] Add layer reorder, toggle, and solo
- [ ] Add layer rename, color-tag, and notes
- [ ] Add layer groups and folders for organisation
- [ ] Add live recompute of the stack with cached intermediate results
- [ ] Add flatten/bake of the stack into the base height on demand
- [ ] Add per-layer undo isolated from other layers
- [ ] Add copy/paste and reuse of layers across terrains
- [ ] Add a stack-cost readout so heavy layers are visible

## Sculpting Tools

- [ ] Add raise and lower sculpting
- [ ] Add smooth and relax sculpting
- [ ] Add flatten with a picked reference height
- [ ] Add a ramp tool between two picked points
- [ ] Add a terrace/step tool with adjustable step height
- [ ] Add a pinch and expand tool
- [ ] Add a twist and swirl tool
- [ ] Add a noise-add tool with selectable noise types
- [ ] Add a stamp/height-import brush from a heightmap image
- [ ] Add a clone/copy-region tool
- [ ] Add a set-absolute-height tool
- [ ] Add a slope-fill tool that builds gradients between contours
- [ ] Add a bridge tool that connects two areas smoothly
- [ ] Add a plateau and mesa tool
- [ ] Add sculpt symmetry across configurable axes
- [ ] Add sculpt masking by slope, height, and painted mask
- [ ] Add live sculpt preview before committing a stroke
- [ ] Add height and slope readout under the cursor
- [ ] Add per-tool strength curves and pressure response
- [ ] Add GPU-accelerated sculpt application for instant response

## Brush System

- [ ] Add a brush engine with radius, strength, falloff, and spacing
- [ ] Add analytic falloff curves (linear, smooth, sphere, custom curve)
- [ ] Add alpha/mask textures as brush shapes
- [ ] Add custom brush import from images
- [ ] Add a brush library with thumbnails and categories
- [ ] Add rotation and per-stamp randomisation
- [ ] Add jitter and scatter of brush stamps
- [ ] Add pen-pressure support for graphics tablets
- [ ] Add pen-tilt and rotation support
- [ ] Add brush spacing and streamline smoothing for fast strokes
- [ ] Add brush presets that bundle tool, shape, and settings
- [ ] Add on-surface brush projection that follows terrain curvature
- [ ] Add a brush-size and strength radial HUD driven by hotkeys
- [ ] Add mirror and radial-symmetry brushing
- [ ] Add a brush cursor preview that shows exact footprint
- [ ] Add lazy-mouse/stabilised strokes for steady lines

## Erosion & Simulation

- [ ] Add hydraulic erosion with rainfall, flow, and sediment transport
- [ ] Add thermal erosion with talus and slope collapse
- [ ] Add wind erosion with directional deposition
- [ ] Add coastal and shoreline erosion around water bodies
- [ ] Add sediment and debris deposition maps
- [ ] Add flow-map output for material blending
- [ ] Add wetness and moisture output
- [ ] Add GPU-accelerated erosion for interactive iteration
- [ ] Add localised erosion confined to a brushed region
- [ ] Add erosion iteration and strength controls
- [ ] Add erosion masks that protect roads, buildings, and flat areas
- [ ] Add erosion presets for common terrain styles
- [ ] Add a real-time erosion preview with adjustable strength
- [ ] Add progressive erosion that can be paused and resumed
- [ ] Add a before/after compare for erosion passes

## Material & Texture Painting

- [ ] Add layer painting onto splat weights with the brush engine
- [ ] Add a terrain material with per-layer albedo, normal, roughness, and height
- [ ] Add height-based blending between layers
- [ ] Add triplanar projection for steep slopes and cliffs
- [ ] Add per-layer tiling, rotation, and scale controls
- [ ] Add macro-variation to hide tiling repetition
- [ ] Add detail-tiling for close-up texture density
- [ ] Add auto-material rules by slope
- [ ] Add auto-material rules by height
- [ ] Add auto-material rules by curvature and flow
- [ ] Add procedural snow line and altitude tinting
- [ ] Add procedural wetness and moss on flat areas
- [ ] Add a paint mask editor with fill, clear, invert, and blur
- [ ] Add a color-tint and vertex-paint layer
- [ ] Add puddle and wetness painting tied to flow maps
- [ ] Add a material-layer palette with drag-and-drop assignment
- [ ] Add live material preview under different lighting
- [ ] Add per-layer normal-strength and specular controls
- [ ] Add layer budgets with warnings when limits are exceeded
- [ ] Add a splat-weight normalization pass to prevent artifacts

## Foliage & Scatter Painting

- [ ] Add density painting for grass, plants, rocks, and props
- [ ] Add a scatter brush with count, spacing, and randomisation
- [ ] Add an eraser and single-item placement mode
- [ ] Add placement rules by slope
- [ ] Add placement rules by height
- [ ] Add placement rules by material layer and mask
- [ ] Add per-item random scale, rotation, and tilt-to-normal
- [ ] Add avoidance rules around roads, water, and other items
- [ ] Add instance clustering for natural grouping
- [ ] Add automatic instancing and LOD for scattered items
- [ ] Add impostor fallback for distant scattered items
- [ ] Add wind and interaction response for painted foliage
- [ ] Add collision and navigation flags per scattered item type
- [ ] Add a scatter palette with weighted item sets
- [ ] Add fill-region and flood-scatter for fast coverage
- [ ] Add density heatmap visualization
- [ ] Add live count and density readouts while painting
- [ ] Add hand-off of terrain scatter into the foliage system

## Biomes & Ecosystem

- [ ] Add a biome definition combining materials, foliage, and scatter rules
- [ ] Add one-click biome painting that applies a whole ecosystem at once
- [ ] Add automatic ecosystem population from height, slope, and moisture
- [ ] Add biome blending and transition zones
- [ ] Add climate and moisture maps that drive biome distribution
- [ ] Add temperature input that shifts biome selection
- [ ] Add a biome library with ready-made presets
- [ ] Add per-biome density and variation controls
- [ ] Add biome masking and manual override painting
- [ ] Add live repopulation when the terrain shape changes
- [ ] Add biome preview and coverage visualization
- [ ] Add capture of an authored region into a reusable biome
- [ ] Add per-biome material and foliage budget checks

## Procedural Generation

- [ ] Add a node-based procedural terrain generator
- [ ] Add noise primitive nodes (perlin, simplex, ridged, billow, voronoi)
- [ ] Add gradient and shape primitive nodes
- [ ] Add combine nodes (add, multiply, blend, min, max)
- [ ] Add warp and distortion nodes
- [ ] Add remap, curve, and clamp nodes
- [ ] Add erosion and deposition nodes inside the graph
- [ ] Add slope, height, and curvature selector nodes
- [ ] Add mask and falloff nodes
- [ ] Add scatter output nodes driven by the graph
- [ ] Add material-weight output nodes driven by the graph
- [ ] Add seed control and deterministic regeneration
- [ ] Add region-limited procedural application inside a brushed area
- [ ] Add a live preview and incremental update of the graph
- [ ] Add graph presets for mountains, deserts, islands, and plains
- [ ] Add graph-to-height baking with resolution control
- [ ] Add blending of procedural output with hand-sculpted edits
- [ ] Add graph node search, comments, and reroute for authoring

## Splines, Roads & Rivers

- [ ] Add a spline tool that conforms to the terrain surface
- [ ] Add spline point insert, delete, and tangent editing
- [ ] Add road splines that flatten and carve the terrain to a profile
- [ ] Add river splines that carve channels and drive flow maps
- [ ] Add cliff and ridge splines with configurable profiles
- [ ] Add wall, fence, and border splines that place scatter along the path
- [ ] Add width, falloff, and edge-blend controls per spline
- [ ] Add automatic material assignment along splines
- [ ] Add banking and slope controls for roads
- [ ] Add intersection and junction handling between splines
- [ ] Add tunnel and bridge cutouts where splines cross terrain
- [ ] Add spline-following prop placement (guardrails, lamps, rocks)
- [ ] Add non-destructive spline layers re-evaluated on terrain edits
- [ ] Add spline snapping to existing terrain features
- [ ] Add a live preview of spline deformation before committing

## Holes, Caves & Overhangs

- [ ] Add hole cutting for cave entrances and tunnels
- [ ] Add hole-aware collision that removes geometry under cutouts
- [ ] Add hole-aware navigation
- [ ] Add optional voxel/mesh overhang regions attached to the heightfield
- [ ] Add seamless blending between heightfield and overhang meshes
- [ ] Add cave-mesh authoring tools
- [ ] Add material assignment for cave and overhang surfaces
- [ ] Add lighting and ambient-occlusion handling inside caves
- [ ] Add hole and overhang preview and validation
- [ ] Add erase and restore of previously cut holes
- [ ] Add streaming support for overhang meshes

## Stamps, Presets & Templates

- [ ] Add a stamp library of mountains, valleys, canyons, craters, and dunes
- [ ] Add drag-and-drop stamp placement with rotate and scale
- [ ] Add blend modes and falloff for stamps
- [ ] Add height-offset and clamp controls per stamp
- [ ] Add capture of a terrain region into a reusable stamp
- [ ] Add stamp thumbnails and categories
- [ ] Add starter templates for common terrain types
- [ ] Add one-click full-terrain generation from a template
- [ ] Add template parameters (size, ruggedness, water level)
- [ ] Add a community/shared stamp and preset import format
- [ ] Add favorites and recent stamps for quick access
- [ ] Add stamp preview before placement

## Water Integration

- [ ] Add lake and pond placement with automatic water level
- [ ] Add river surfaces that follow river splines
- [ ] Add an ocean/sea level with shoreline blending
- [ ] Add shoreline foam and wetness transition materials
- [ ] Add sand and mud transition zones around water
- [ ] Add water-depth-driven color and opacity
- [ ] Add flow direction and speed authoring for rivers
- [ ] Add waterfalls where rivers meet cliffs
- [ ] Add automatic terrain carving under placed water
- [ ] Add water-level preview and flood visualization
- [ ] Add underwater terrain material handling

## Import / Export & Real-World Data

- [ ] Add heightmap import in common image formats
- [ ] Add heightmap import in raw and high-bit-depth formats
- [ ] Add heightmap export
- [ ] Add real-world elevation import from standard geographic datasets
- [ ] Add georeferencing with real-world scale and coordinates
- [ ] Add satellite/aerial imagery import as a base material layer
- [ ] Add weight/splat map import and export
- [ ] Add mesh export of the terrain surface for external tools
- [ ] Add round-trip import/export that preserves layers and scatter
- [ ] Add resolution resampling on import with quality options
- [ ] Add tiled import for very large datasets
- [ ] Add import preview with automatic level and scale detection
- [ ] Add unit and coordinate-system conversion on import
- [ ] Add batch import of multiple tiles

## Streaming & Large Worlds

- [ ] Add background streaming of terrain tiles around the camera
- [ ] Add level-of-detail selection and seamless tile stitching
- [ ] Add an editor overview map for navigating large terrains
- [ ] Add per-tile edit locking and check-out for team workflows
- [ ] Add memory budgets and residency diagnostics for edited terrain
- [ ] Add partial save of only modified tiles
- [ ] Add a proxy/low-detail representation for distant tiles while editing
- [ ] Add async tile load and unload without viewport stalls
- [ ] Add a streaming radius and priority around the edit cursor
- [ ] Add tile-boundary seam validation during streaming

## Collision & Navigation

- [ ] Add automatic collision generation from the heightfield
- [ ] Add hole-aware collision that removes geometry under cutouts
- [ ] Add per-layer physics materials (friction, footstep, surface type)
- [ ] Add collision LOD separate from render LOD
- [ ] Add incremental collision rebuild limited to edited tiles
- [ ] Add automatic navigation-mesh regeneration on terrain edits
- [ ] Add navigation flags painted per region (walkable, blocked, water)
- [ ] Add collision preview and inspection overlay
- [ ] Add async collision and navigation rebuild off the main thread

## User-Friendly Authoring

- [ ] Add a guided "create terrain" wizard with size and style presets
- [ ] Add sensible smart defaults so a usable terrain exists immediately
- [ ] Add a simple tool palette with large icons and plain-language names
- [ ] Add tooltips and inline hints on every tool
- [ ] Add a short interactive tutorial for first-time users
- [ ] Add a beginner mode that hides advanced parameters
- [ ] Add real-time preview of every tool before committing
- [ ] Add always-available undo, redo, and history scrubbing
- [ ] Add a before/after compare toggle
- [ ] Add one-click biome and material application
- [ ] Add one-click auto-erosion for natural-looking terrain
- [ ] Add one-click auto-material based on slope and height
- [ ] Add draggable on-screen handles instead of numeric-only fields
- [ ] Add live suggestions ("add erosion here", "smooth this cliff")
- [ ] Add named quality presets (draft, standard, high) with no jargon
- [ ] Add auto-save and crash-safe recovery of terrain edits
- [ ] Add graphics-tablet and touch support for natural sculpting
- [ ] Add a distraction-free full-viewport sculpt mode
- [ ] Add contextual toolbars that show only relevant options
- [ ] Add friendly warnings and one-click fixes for common mistakes
- [ ] Add a gallery of example terrains that can be opened and tweaked
- [ ] Add consistent, reversible behaviour across all tools

## Editing Performance & Feedback

- [ ] Add GPU-accelerated brush application for instant response on large terrains
- [ ] Add asynchronous recompute so editing never freezes the viewport
- [ ] Add incremental updates limited to affected tiles and layers
- [ ] Add throttled auto-save that never interrupts editing
- [ ] Add a live statistics panel (size, memory, layer count, instance count)
- [ ] Add progress indicators for long operations with cancel support
- [ ] Add a performance budget with gentle warnings before limits are hit
- [ ] Add profiling of sculpt, erosion, paint, and rebuild cost
- [ ] Add background prebaking of expensive layers when idle

## Testing & Validation

- [ ] Add tile-seam continuity validation across the whole terrain
- [ ] Add height, weight, and mask range validation
- [ ] Add scatter placement determinism tests for a fixed seed
- [ ] Add import/export round-trip fidelity tests
- [ ] Add erosion reproducibility tests
- [ ] Add procedural-graph reproducibility tests
- [ ] Add undo/redo integrity tests across every tool
- [ ] Add large-terrain streaming and memory-budget stress tests
- [ ] Add golden-image tests for sculpt, paint, and material results
- [ ] Add collision and navigation regeneration correctness tests

# 5 · Foliage / Vegetation / Scatter

## Foliage Data Model & Types

- [ ] Add a foliage-type asset (mesh set, materials, density, scale range, collision)
- [ ] Add grouping of foliage types into reusable palettes
- [ ] Add per-instance data (transform, scale, rotation, color, health, seed)
- [ ] Add a compact per-cell instance buffer format
- [ ] Add spatial partitioning of instances into cells
- [ ] Add weighted item sets for randomized placement
- [ ] Add per-type placement constraints (slope, height, surface, spacing)
- [ ] Add per-type render settings (LOD, shadows, wind, collision)
- [ ] Add a versioned foliage-layer asset format with migration
- [ ] Add references from foliage instances back to their source type
- [ ] Add stable instance identifiers for edit, save, and streaming
- [ ] Add per-cell bounds and checksums for change detection
- [ ] Add copy, crop, and merge operations on foliage layers

## ECS Integration & Bulk Scale

- [ ] Store foliage instances directly in archetype/chunk storage in a data-oriented layout
- [ ] Represent instance transforms and attributes as components
- [ ] Process instance chunks with hot SIMD kernels
- [ ] Add bulk spawn of millions of instances through the deferred command buffer
- [ ] Add bulk despawn through the deferred command buffer
- [ ] Add batched structural changes that avoid per-instance archetype moves
- [ ] Add parallel instance generation across the worker pool
- [ ] Add SIMD-vectorized culling kernels over instance chunks
- [ ] Add SIMD-vectorized LOD-selection kernels over instance chunks
- [ ] Add SIMD-vectorized wind kernels over instance chunks
- [ ] Add chunk-iteration queries with no per-entity overhead
- [ ] Add cache-friendly chunk layouts tuned by the chunk-size advisor
- [ ] Add zero-copy upload from instance chunks to GPU instance buffers
- [ ] Add streaming of instance chunks in and out without stalls
- [ ] Add a hybrid model where dense instances stay packed and only interactive ones become full entities
- [ ] Add lightweight instance representation that avoids full entity cost at extreme counts
- [ ] Add job-graph scheduling of scatter, culling, animation, and upload stages
- [ ] Add memory-traffic-aware batch sizes for instance processing
- [ ] Add deterministic parallel scatter stable regardless of thread count
- [ ] Add support for tens of millions of instances within fixed memory budgets
- [ ] Add near-unlimited GPU-generated grass with no per-blade CPU cost
- [ ] Add bulk transform and attribute edits applied across chunks in parallel
- [ ] Add a scale and throughput stress harness targeting extreme instance counts

## Placement & Painting Tools

- [ ] Add a paint brush that scatters instances on any surface
- [ ] Add single-instance placement with snapping and alignment
- [ ] Add an eraser with type filtering
- [ ] Add fill-region and flood placement inside a selection
- [ ] Add a lasso and polygon selection for bulk edits
- [ ] Add a replace tool that swaps one type for another
- [ ] Add adjust tools for scale on existing instances
- [ ] Add adjust tools for rotation and alignment on existing instances
- [ ] Add surface projection onto meshes
- [ ] Add surface projection onto terrain and water
- [ ] Add align-to-normal, align-to-world, and random-tilt options
- [ ] Add spacing, density, and jitter controls per stroke
- [ ] Add mask painting to limit or protect regions
- [ ] Add spline-based placement along paths and edges
- [ ] Add copy, paste, and duplicate of instance selections
- [ ] Add nudge, rotate, and scale gizmos for selections
- [ ] Add live preview of placement before committing
- [ ] Add a brush cursor that shows exact footprint and count

## Procedural Scatter Engine

- [ ] Add rule-based scatter driven by slope, height, and curvature
- [ ] Add scatter masks from painted maps
- [ ] Add density maps and moisture/climate inputs
- [ ] Add Poisson-disk and blue-noise distribution for natural spacing
- [ ] Add clustering and grouping rules for clumps and groves
- [ ] Add avoidance rules around roads, water, and structures
- [ ] Add avoidance between different foliage types
- [ ] Add layering rules (canopy, understory, ground cover) with competition
- [ ] Add ecosystem simulation that populates from environmental inputs
- [ ] Add seed control and deterministic regeneration
- [ ] Add region-limited procedural scatter inside a brushed area
- [ ] Add a node-based scatter graph
- [ ] Add live preview and incremental update of the scatter graph
- [ ] Add live repopulation when the underlying surface changes
- [ ] Add scatter presets for forests, meadows, deserts, and rocky fields
- [ ] Add scatter-result caching to avoid full regeneration

## Instanced Rendering

- [ ] Add hardware-instanced rendering of foliage meshes
- [ ] Add per-instance custom data for color, wind, and variation
- [ ] Add batching of instances by type, material, and cell
- [ ] Add indirect draw submission for large instance counts
- [ ] Add per-instance transform compression
- [ ] Add a depth-only instanced pass
- [ ] Add a shadow-only instanced pass
- [ ] Add a merged draw path for many small foliage types
- [ ] Add dynamic instance buffers updated without stalls
- [ ] Add per-instance selection and highlight for editing
- [ ] Add instance color and tint variation in the shader
- [ ] Add draw-call and instance-count stats per type

## GPU-Driven Culling & Density

- [ ] Add GPU per-instance frustum culling
- [ ] Add GPU occlusion culling for foliage instances
- [ ] Add cluster/cell culling before per-instance culling
- [ ] Add distance-based density fade with per-type cutoff
- [ ] Add screen-coverage-based instance skipping
- [ ] Add a global density scalar for quality scaling
- [ ] Add GPU compaction of visible instances into draw lists
- [ ] Add per-view culling for the main view
- [ ] Add per-view culling for shadow passes
- [ ] Add per-view culling for reflections
- [ ] Add culling debug visualization and counters
- [ ] Add a fallback CPU culling path for unsupported hardware

## Level of Detail & Impostors

- [ ] Add per-instance LOD selection by screen size and distance
- [ ] Add smooth LOD transitions with dithered cross-fade
- [ ] Add billboard fallback at distance
- [ ] Add octahedral impostor capture
- [ ] Add octahedral impostor rendering
- [ ] Add automatic impostor generation from source meshes
- [ ] Add impostor lighting that responds to scene light direction
- [ ] Add LOD bias and hysteresis to avoid popping
- [ ] Add merged distant-foliage meshes for far ranges
- [ ] Add per-type LOD distance and budget controls
- [ ] Add LOD and impostor transition debug visualization
- [ ] Add shadow LOD independent from render LOD

## Grass & Ground Cover

- [ ] Add dense grass generation from density maps
- [ ] Add GPU-generated grass blades with per-blade variation
- [ ] Add mesh-card grass option
- [ ] Add geometry-blade grass option
- [ ] Add camera-distance grass density and radius controls
- [ ] Add grass color variation from ground material and masks
- [ ] Add grass placement that follows terrain and painted surfaces
- [ ] Add grass response to wind
- [ ] Add grass response to interaction and trampling
- [ ] Add ground-cover clutter (pebbles, twigs, flowers) as cheap instances
- [ ] Add grass shadow handling tuned for density
- [ ] Add grass depth and occlusion handling
- [ ] Add grass density and coverage debug visualization

## Wind & Animation

- [ ] Add a wind source with direction, strength, and gusts
- [ ] Add wind zones with local overrides and falloff
- [ ] Add trunk-sway wind motion
- [ ] Add branch-bend wind motion
- [ ] Add leaf-flutter wind motion
- [ ] Add per-type wind stiffness and response tuning
- [ ] Add gust and turbulence noise for natural motion
- [ ] Add vertex-animation driven by wind in the foliage shader
- [ ] Add wind response scaled by instance size and age
- [ ] Add a shared wind source consumable by clouds, cloth, and particles
- [ ] Add wind debug visualization

## Interaction & Physics Response

- [ ] Add bending of grass and plants around characters
- [ ] Add bending around dynamic objects
- [ ] Add a trample/flow map that persists recent interaction
- [ ] Add per-instance push from physics bodies
- [ ] Add push response from explosions and impulses
- [ ] Add recovery and spring-back after interaction
- [ ] Add cutting and destruction states for foliage
- [ ] Add burning and scorch states for foliage
- [ ] Add interaction budgets and range limits for performance
- [ ] Add interaction debug visualization

## Lighting & Shading

- [ ] Add two-sided foliage shading
- [ ] Add translucency for thin leaves and blades
- [ ] Add subsurface scattering for foliage
- [ ] Add wind-aware normals for believable shading in motion
- [ ] Add ambient occlusion for dense foliage
- [ ] Add contact shadows for foliage
- [ ] Add distance-based shading simplification for far instances
- [ ] Add consistent lighting between meshes and their impostors
- [ ] Add seasonal and wetness tint hooks in the shader

## Seasons & Variation

- [ ] Add per-instance color and hue variation
- [ ] Add spring, summer, autumn, and winter tint sets
- [ ] Add seasonal density changes
- [ ] Add health, wilt, and dryness states driven by data
- [ ] Add snow accumulation on foliage
- [ ] Add wetness accumulation on foliage
- [ ] Add age and growth variation across instances
- [ ] Add smooth transitions when season or weather changes
- [ ] Add a shared climate/temperature input driving variation

## Streaming & Large Worlds

- [ ] Add background streaming of foliage cells around the camera
- [ ] Add load/unload of instance buffers by residency budget
- [ ] Add proxy representations for distant regions while streaming
- [ ] Add partial save of only modified foliage cells
- [ ] Add memory budgets and residency diagnostics for foliage
- [ ] Add async build of instance buffers off the main thread
- [ ] Add streaming priority around the camera and edit cursor
- [ ] Add seam handling so cells match at boundaries

## Collision & Navigation

- [ ] Add optional per-type collision (capsule, box, mesh)
- [ ] Add navigation blocking for trees and large obstacles
- [ ] Add walkable-through flags for grass and small plants
- [ ] Add collision LOD independent from render LOD
- [ ] Add automatic navigation regeneration when large foliage changes
- [ ] Add incremental collision updates limited to changed cells
- [ ] Add collision and navigation debug overlays

## Authoring UX

- [ ] Add a foliage palette with thumbnails, categories, and drag-and-drop
- [ ] Add biome brushes that paint whole vegetation sets at once
- [ ] Add ready-made presets for common environments
- [ ] Add sensible defaults so painted foliage looks good immediately
- [ ] Add real-time preview of placement
- [ ] Add always-available undo and redo
- [ ] Add plain-language controls and hover hints
- [ ] Add a beginner mode that hides advanced scatter parameters
- [ ] Add one-click "auto-populate this area" from environment rules
- [ ] Add on-screen density and count readouts while painting
- [ ] Add a distraction-free painting mode
- [ ] Add a gallery of example vegetation setups to open and tweak

## Performance & Budgets

- [ ] Add instance-count and memory budgets with gentle warnings
- [ ] Add automatic density scaling to hold a target framerate
- [ ] Add a live statistics panel (instances, draw calls, memory, culled)
- [ ] Add a quality-preset mapping (draft, standard, high) with no jargon
- [ ] Add profiling of foliage culling cost
- [ ] Add profiling of LOD and submission cost
- [ ] Add async and incremental updates so editing never stalls the viewport
- [ ] Add per-type cost attribution in the profiler

## Testing & Validation

- [ ] Add deterministic scatter tests for a fixed seed
- [ ] Add instance placement and constraint validation
- [ ] Add LOD transition tests
- [ ] Add impostor generation and rendering tests
- [ ] Add culling correctness tests across views
- [ ] Add streaming and memory-budget stress tests
- [ ] Add bulk spawn/despawn throughput tests at extreme counts
- [ ] Add undo/redo integrity tests across every tool
- [ ] Add golden-image tests for grass, wind, and impostor rendering

# 6 · Sky / Atmosphere / Weather / Time-of-Day

## Sky & Atmosphere Model

- [ ] Add a physically based atmospheric-scattering sky
- [ ] Add Rayleigh scattering with configurable coefficients
- [ ] Add Mie scattering with configurable coefficients and anisotropy
- [ ] Add a sky-view lookup table for cheap full-screen sky evaluation
- [ ] Add a transmittance lookup table
- [ ] Add aerial perspective applied to distant geometry
- [ ] Add multiple-scattering approximation for realistic daylight
- [ ] Add planet curvature and configurable atmosphere height
- [ ] Add ground-albedo influence on sky and horizon color
- [ ] Add ozone absorption for accurate blue and sunset tones
- [ ] Add altitude and air-density controls
- [ ] Add a fast analytic sky option for low-end hardware
- [ ] Add a captured/HDR sky option with runtime tint and rotation
- [ ] Add automatic sky-light and ambient capture from the atmosphere
- [ ] Add horizon haze and blending into distance fog
- [ ] Add debug visualization of scattering and lookup tables

## Sun, Moon & Celestial Bodies

- [ ] Add a sun disk with adjustable size and intensity
- [ ] Add sun limb darkening
- [ ] Add a moon with orientation and surface texture
- [ ] Add moon phase computation and rendering
- [ ] Add astronomically correct sun position by date, time, and latitude
- [ ] Add astronomically correct moon position
- [ ] Add manual sun and moon positioning for art-directed skies
- [ ] Add earthshine and moonlight contribution at night
- [ ] Add support for multiple suns or custom celestial light sources
- [ ] Add solar and lunar eclipse handling
- [ ] Add a sun-glow and bloom response tied to intensity
- [ ] Add a lens-flare response for the sun

## Stars & Night Sky

- [ ] Add a star field with realistic brightness distribution
- [ ] Add a milky-way band and deep-sky background
- [ ] Add constellation and named-star placement
- [ ] Add visible planets positioned by date and time
- [ ] Add star twinkle
- [ ] Add atmospheric extinction of stars near the horizon
- [ ] Add shooting stars and meteor showers
- [ ] Add star rotation synchronized with the day/night cycle
- [ ] Add auroras for polar and stylized skies
- [ ] Add a night-sky brightness and light-pollution control

## Volumetric Clouds

- [ ] Add ray-marched volumetric clouds with layered coverage
- [ ] Add cloud types (cumulus, stratus, cirrus) driven by presets
- [ ] Add cloud coverage control
- [ ] Add cloud density and altitude controls
- [ ] Add cloud lighting with multiple scattering
- [ ] Add silver-lining and powder terms for realism
- [ ] Add cloud shadows cast onto the world
- [ ] Add wind-driven cloud movement and evolution over time
- [ ] Add weather-driven coverage that thickens before storms
- [ ] Add cheap 2D cloud-plane fallback for low-end hardware
- [ ] Add temporal reprojection to keep cloud cost low
- [ ] Add cloud-shape authoring from noise and profile curves
- [ ] Add horizon and high-altitude cloud layers
- [ ] Add quality presets scaling ray-march steps and resolution
- [ ] Add cloud rendering into reflections and distant views
- [ ] Add cloud cost budgets and diagnostics

## Time of Day

- [ ] Add a day/night cycle with adjustable day length
- [ ] Add a time-of-day value driving sun, moon, sky, and lighting
- [ ] Add pause, scrub, and playback-speed control of time
- [ ] Add date, season, and latitude inputs
- [ ] Add keyframed sky and lighting profiles across the day
- [ ] Add smooth interpolation between time-of-day keyframes
- [ ] Add sunrise, noon, sunset, and night presets
- [ ] Add golden-hour and blue-hour tuning
- [ ] Add season-driven sun path and day-length changes
- [ ] Add scripting hooks for time events (dawn, dusk, midnight)
- [ ] Add save and restore of the current time state
- [ ] Add a time-of-day timeline editor with a 24-hour track

## Weather System

- [ ] Add a weather-state model (clear, cloudy, rain, storm, snow, fog)
- [ ] Add smooth transitions and blending between weather states
- [ ] Add a weather timeline and scheduler
- [ ] Add randomized and seeded weather sequences
- [ ] Add localized weather zones with falloff
- [ ] Add intensity control per weather state
- [ ] Add storm build-up with darkening sky and rising wind
- [ ] Add lightning generation
- [ ] Add thunder with distance-based delay
- [ ] Add weather presets and a preset blending system
- [ ] Add gameplay and scripting hooks for weather changes
- [ ] Add save and restore of the current weather state
- [ ] Add climate profiles that bias weather probability by region
- [ ] Add deterministic weather for replays and networked sessions

## Precipitation & Accumulation

- [ ] Add rain with adjustable density, speed, and angle
- [ ] Add snow with drift and settling behavior
- [ ] Add hail and sleet variants
- [ ] Add rain splashes and ripples on surfaces
- [ ] Add rain interaction with water surfaces
- [ ] Add camera-relative precipitation that follows the view
- [ ] Add occlusion so precipitation stops under cover
- [ ] Add surface wetness that builds and dries over time
- [ ] Add puddle formation in low areas during rain
- [ ] Add snow accumulation on upward-facing surfaces
- [ ] Add ice and frost formation in cold conditions
- [ ] Add gradual melt and evaporation as weather clears
- [ ] Add wind influence on precipitation direction
- [ ] Add precipitation particle budgets and quality scaling

## Fog & Atmospheric Effects

- [ ] Add exponential height fog with color and density controls
- [ ] Add volumetric fog with light scattering
- [ ] Add light shafts and god rays from the sun
- [ ] Add light shafts from local lights
- [ ] Add ground mist and low-lying fog banks
- [ ] Add fog color driven by time of day and sky
- [ ] Add distance and depth fog blended with aerial perspective
- [ ] Add localized fog volumes with falloff
- [ ] Add heat-haze and shimmer effects
- [ ] Add fog quality scaling and cost budgets

## Lighting Integration

- [ ] Drive the main directional light from the sun position
- [ ] Drive a secondary directional light from the moon at night
- [ ] Update sky-light and ambient from the current atmosphere on change
- [ ] Add color-temperature shifts across the day
- [ ] Add automatic exposure adaptation across day and night
- [ ] Add cloud and weather dimming of direct light
- [ ] Add lightning flashes as transient scene lighting
- [ ] Update global illumination when the sky changes significantly
- [ ] Add night artificial-light response (streetlights on at dusk)
- [ ] Add shadow color and softness tied to sky conditions
- [ ] Add throttled sky-light updates to control cost

## Wind Integration

- [ ] Add a global wind driven by weather and time
- [ ] Add gusts and turbulence that ramp with storms
- [ ] Add wind direction changes over time
- [ ] Add wind zones with local overrides
- [ ] Expose wind to foliage from one shared source
- [ ] Expose wind to cloth and particles from the same source
- [ ] Expose wind to clouds from the same source
- [ ] Add wind strength visualization and debug readout

## Weather Effects on the World

- [ ] Add dynamic surface wetness response in materials
- [ ] Add snow material response tied to accumulation
- [ ] Add ice material response tied to freezing
- [ ] Add puddle reflections and ripple response
- [ ] Add lightning strike points with world impact and light
- [ ] Add wind-driven debris and leaves during storms
- [ ] Add temperature as a shared value driving snow, ice, and melt
- [ ] Add weather influence on foliage color and health

## Audio Integration

- [ ] Add ambient rain and storm soundscapes
- [ ] Add ambient wind soundscapes
- [ ] Add thunder synchronized with lightning and distance
- [ ] Add smooth audio transitions between weather states
- [ ] Add interior and sheltered attenuation of weather audio
- [ ] Add time-of-day ambience (birds at dawn, crickets at night)
- [ ] Add audio intensity tied to weather strength

## Authoring & Presets

- [ ] Add a time-of-day keyframe editor with a day timeline
- [ ] Add a sky and atmosphere preset library
- [ ] Add a weather preset library with blend weights
- [ ] Add curve-based control of color over time
- [ ] Add curve-based control of intensity and density over time
- [ ] Add climate presets that bundle sky, weather, and lighting
- [ ] Add capture of the current look into a reusable preset
- [ ] Add layering of art-directed overrides on top of physical simulation
- [ ] Add copy and share of sky and weather presets
- [ ] Add preset thumbnails and categories

## User-Friendly Authoring

- [ ] Add a simple time-of-day slider with live preview
- [ ] Add one-click weather buttons (clear, rain, storm, snow, fog)
- [ ] Add plain-language sliders (cloudiness, wind, wetness, warmth)
- [ ] Add sensible defaults that produce a good sky immediately
- [ ] Add a beginner mode that hides physical parameters
- [ ] Add "make it dramatic" and "make it calm" one-click looks
- [ ] Add a scrubbable day preview to see the sky across 24 hours
- [ ] Add before/after compare for preset changes
- [ ] Add hover hints and a short guided tour
- [ ] Add a gallery of example skies and weather to open and tweak
- [ ] Add always-available undo and redo for every change

## Data-Driven & Scripting

- [ ] Add a weather-schedule asset for scripted campaigns
- [ ] Add events for weather changes consumable by gameplay
- [ ] Add events for time-of-day milestones consumable by gameplay
- [ ] Add a scripting API to query and set time
- [ ] Add a scripting API to query and set weather and wind
- [ ] Add conditional weather triggered by location or gameplay state
- [ ] Add deterministic weather for replays and networked sessions
- [ ] Add persistence of full sky and weather state in saves

## Performance & Quality Scaling

- [ ] Add quality presets scaling sky, cloud, and fog cost
- [ ] Add lookup-table caching for sky and aerial perspective
- [ ] Add temporal amortization of expensive atmosphere work
- [ ] Add temporal amortization of expensive cloud work
- [ ] Add resolution scaling for volumetric passes
- [ ] Add budgets and diagnostics for sky, cloud, and weather cost
- [ ] Add automatic downscaling to hold target framerate

## Testing & Validation

- [ ] Add golden-image tests across representative times of day
- [ ] Add golden-image tests across weather states
- [ ] Add weather-transition determinism tests
- [ ] Add sky lookup-table validation
- [ ] Add sun-position accuracy tests by time, date, and latitude
- [ ] Add moon-phase and moon-position accuracy tests
- [ ] Add save/restore fidelity tests for sky and weather state
- [ ] Add performance budget tests for volumetric passes

# 7 · Water / Ocean

## Water Data Model & Body Types

- [ ] Add a water-body asset with type, bounds, level, and material reference
- [ ] Add body types (ocean, sea, lake, pond, river, stream, pool, custom volume)
- [ ] Add a water-volume representation for buoyancy and containment
- [ ] Add per-body surface level and depth field
- [ ] Add a shared water material with per-body overrides
- [ ] Add per-body simulation settings (wave scale, flow, calmness)
- [ ] Add a spline definition for rivers and shaped water
- [ ] Add a mask layer that limits where a body renders
- [ ] Add tiling and infinite-extent flags for oceans
- [ ] Add per-body bounds, checksums, and dirty tracking
- [ ] Add a versioned water asset format with migration
- [ ] Add copy, crop, and merge operations on water bodies
- [ ] Add references from water bodies to terrain tiles they touch

## One-Click Water Creation

- [ ] Add a one-click ocean that fills the world to a chosen level
- [ ] Add a one-click sea bounded to a region
- [ ] Add a one-click lake that auto-fills a terrain basin
- [ ] Add a one-click pond placed under the cursor
- [ ] Add a one-click pool with straight edges and a fixed level
- [ ] Add a one-click river started by drawing a spline
- [ ] Add automatic water-level detection from surrounding terrain
- [ ] Add automatic basin detection and flood-fill for lakes
- [ ] Add drag-to-place sizing with live preview
- [ ] Add sensible defaults so new water looks good immediately
- [ ] Add a water-type picker with clear icons and names
- [ ] Add per-type presets (calm lake, rough sea, tropical ocean, mountain stream)
- [ ] Add automatic shoreline blending on creation
- [ ] Add a guided create-water wizard for first-time users

## Spline Water Authoring

- [ ] Add a river spline tool that follows and carves the terrain
- [ ] Add spline point insert, delete, and tangent editing
- [ ] Add a width profile along the spline
- [ ] Add a depth profile along the spline
- [ ] Add automatic flow direction derived from spline and slope
- [ ] Add flow-speed control along the spline
- [ ] Add bank blending and shoreline width per segment
- [ ] Add meander and natural-curve smoothing helpers
- [ ] Add branching and confluence handling where rivers merge
- [ ] Add automatic waterfalls where the spline crosses cliffs
- [ ] Add rapids and turbulence zones on steep segments
- [ ] Add automatic terrain carving under the river channel
- [ ] Add snapping of river ends to lakes, seas, and other rivers
- [ ] Add spline-following foam, debris, and rocks
- [ ] Add non-destructive spline re-evaluation when terrain changes
- [ ] Add a live preview of the river before committing

## Ocean Simulation

- [ ] Add spectral ocean simulation from a wave spectrum
- [ ] Add a configurable wave spectrum (wind speed, fetch, direction)
- [ ] Add multi-cascade waves covering large and small scales
- [ ] Add Gerstner-wave synthesis for sharp crests
- [ ] Add choppiness and crest-sharpening controls
- [ ] Add wind-driven wave direction and alignment
- [ ] Add swell layered on top of local wind waves
- [ ] Add a sea-state scale from calm to storm
- [ ] Add GPU evaluation of the wave displacement and normals
- [ ] Add foam generation from wave steepness and folding
- [ ] Add per-cascade tiling and detail controls
- [ ] Add wave height, slope, and Jacobian sampling on the CPU for gameplay
- [ ] Add deterministic ocean state for replays and networking
- [ ] Add smooth transitions between sea states
- [ ] Add ocean simulation cost budgets and quality scaling

## Waves (Lakes, Rivers & Ambient)

- [ ] Add Gerstner waves for lakes and enclosed water
- [ ] Add ambient ripple detail from noise
- [ ] Add wind-driven ripple direction and strength
- [ ] Add wave damping near shorelines and in sheltered areas
- [ ] Add fetch-based wave scaling by open-water distance
- [ ] Add river surface chop tied to flow speed
- [ ] Add small-scale surface detail normals
- [ ] Add CPU height sampling for enclosed-water gameplay

## Water Surface Rendering

- [ ] Add depth-based water color and absorption
- [ ] Add transparency and Fresnel-driven reflectivity
- [ ] Add screen-space reflections for the water surface
- [ ] Add planar reflections as a higher-quality option
- [ ] Add reflection-probe fallback where screen data is missing
- [ ] Add refraction with depth-aware distortion
- [ ] Add animated normal maps blended across scales
- [ ] Add detail normals for close-up surface richness
- [ ] Add sun and moon specular glitter
- [ ] Add subsurface scattering for shallow and backlit water
- [ ] Add sky and cloud reflection tied to time of day
- [ ] Add edge softening where water meets geometry
- [ ] Add a distance detail fade to hide tiling
- [ ] Add color presets (tropical, murky, glacial, swamp)
- [ ] Add a live surface preview under different lighting

## Foam & Whitecaps

- [ ] Add whitecap foam from wave crests
- [ ] Add shoreline foam that follows the waterline
- [ ] Add foam around objects intersecting the surface
- [ ] Add foam from flow turbulence and rapids
- [ ] Add foam from waterfall impact points
- [ ] Add foam texture blending and animation
- [ ] Add foam persistence and dissipation over time
- [ ] Add foam density and coverage controls
- [ ] Add foam response to wind strength
- [ ] Add a foam mask for hand-painted control

## Shoreline & Terrain Blending

- [ ] Add depth-based shoreline transition
- [ ] Add wet-sand and wet-rock material response near the waterline
- [ ] Add an animated foam line at the shore
- [ ] Add wave run-up and swash on beaches
- [ ] Add automatic shoreline detection from terrain height
- [ ] Add soft intersection blending to hide hard edges
- [ ] Add tide level with slow rise and fall
- [ ] Add shoreline debris and seaweed placement
- [ ] Add underwater terrain darkening and color grading
- [ ] Add automatic terrain wetness zone around water bodies

## Underwater Rendering

- [ ] Add underwater fog with depth-based color and density
- [ ] Add underwater caustics on submerged surfaces
- [ ] Add underwater god rays from the sun
- [ ] Add surface-from-below rendering with total internal reflection
- [ ] Add screen distortion and blur underwater
- [ ] Add rising bubbles and particulate matter
- [ ] Add depth-based murkiness and visibility falloff
- [ ] Add a smooth camera transition crossing the surface
- [ ] Add underwater ambient audio handoff

## Flow & Currents

- [ ] Add flow maps driving surface motion
- [ ] Add river flow derived from spline and slope
- [ ] Add directional ocean currents
- [ ] Add flow-driven foam and debris transport
- [ ] Add whirlpools and vortex zones
- [ ] Add rapids with increased turbulence and foam
- [ ] Add flow authoring and painting tools
- [ ] Add flow visualization overlay
- [ ] Add flow forces applied to floating objects

## Water Interaction & Dynamics

- [ ] Add dynamic ripples from objects touching the surface
- [ ] Add wakes trailing moving objects
- [ ] Add splashes on impact and exit
- [ ] Add an interactive height-field simulation for local disturbances
- [ ] Add ripple propagation and reflection off shores
- [ ] Add object-size-scaled disturbance strength
- [ ] Add persistent trails that fade over time
- [ ] Add interaction budgets and active-region limits
- [ ] Add coupling between the interactive sim and the base waves
- [ ] Add interaction debug visualization

## Buoyancy & Physics

- [ ] Add a water-height sampling API for gameplay and physics
- [ ] Add buoyancy forces for floating objects
- [ ] Add submersion-depth and displaced-volume computation
- [ ] Add water drag and damping on submerged bodies
- [ ] Add current and flow forces pushing objects
- [ ] Add multi-point buoyancy sampling for stable floating
- [ ] Add wave-driven bobbing and rocking of floating objects
- [ ] Add a swimmable-volume query for characters
- [ ] Add enter-water and exit-water physics events
- [ ] Add buoyancy determinism for replays and networking

## Caustics

- [ ] Add projected animated caustics from the surface
- [ ] Add depth-aware caustics that fade with distance
- [ ] Add caustics on terrain and submerged objects
- [ ] Add sun and moon direction driving caustic projection
- [ ] Add caustic intensity tied to water clarity
- [ ] Add caustics above water from shallow ripples
- [ ] Add caustics quality scaling and budgets

## Waterfalls & Rapids

- [ ] Add waterfall meshes generated from river cliffs
- [ ] Add falling-water particles and streaks
- [ ] Add spray and mist at the base
- [ ] Add foam and turbulence at the impact pool
- [ ] Add waterfall lighting and translucency
- [ ] Add wetness on rocks behind and beside the fall
- [ ] Add waterfall audio handoff
- [ ] Add rapids surface treatment on steep river segments

## Wetness & Splash Response

- [ ] Add surfaces becoming wet near and after contact with water
- [ ] Add splash decals on nearby surfaces
- [ ] Add drip and runoff after leaving water
- [ ] Add a wet-to-dry transition over time
- [ ] Add shared wetness input with the weather system
- [ ] Add character and object wet-material response

## Weather & Sky Integration

- [ ] Drive ocean wave height from wind strength
- [ ] Add rain ripples on the water surface
- [ ] Add storm sea-state escalation
- [ ] Reflect sky, clouds, and time-of-day color on the surface
- [ ] Add freezing to ice in cold conditions
- [ ] Add thaw from ice back to open water
- [ ] Add calm-to-storm and storm-to-calm transitions
- [ ] Add wind direction alignment of waves and foam
- [ ] Add shared wind and temperature inputs from the sky system

## Level of Detail & Tiling

- [ ] Add a projected-grid or clipmap water mesh
- [ ] Add distance-based surface LOD
- [ ] Add infinite ocean tiling without visible seams
- [ ] Add detail and foam fade with distance
- [ ] Add streaming of large water regions
- [ ] Add a low-detail distant-water representation
- [ ] Add LOD transition smoothing to avoid popping

## Collision & Navigation

- [ ] Add water-volume triggers for enter and exit
- [ ] Add swimmable-region tagging for navigation
- [ ] Add navigation flags for water-blocked and swim areas
- [ ] Add depth zones (shallow, wadeable, deep)
- [ ] Add flow-affected navigation cost
- [ ] Add collision handoff for solid ice surfaces

## Data-Driven & Scripting

- [ ] Add a scripting API to query water height, depth, and flow
- [ ] Add enter-water and exit-water gameplay events
- [ ] Add flow and current trigger volumes
- [ ] Add a sea-state schedule asset
- [ ] Add conditional water state from gameplay and weather
- [ ] Add deterministic water for replays and networked sessions
- [ ] Add persistence of water and sea state in saves

## Authoring UX

- [ ] Add a water palette with body types, materials, and presets
- [ ] Add plain-language sliders (calmness, wave height, murkiness, flow)
- [ ] Add real-time preview of every change
- [ ] Add always-available undo and redo
- [ ] Add draggable on-screen handles for level, size, and flow
- [ ] Add a beginner mode that hides simulation parameters
- [ ] Add one-click looks (calm, choppy, stormy, tropical, murky)
- [ ] Add hover hints and a short guided tour
- [ ] Add on-screen readouts of depth and flow under the cursor
- [ ] Add a gallery of example water setups to open and tweak
- [ ] Add contextual toolbars per water tool

## Performance & Quality Scaling

- [ ] Add quality presets scaling simulation and rendering cost
- [ ] Add simulation-resolution scaling for waves and interaction
- [ ] Add reflection-quality scaling
- [ ] Add temporal amortization of reflections and caustics
- [ ] Add budgets and diagnostics for water cost
- [ ] Add automatic downscaling to hold target framerate
- [ ] Add async simulation off the main thread

## Testing & Validation

- [ ] Add golden-image tests across calm, choppy, and storm states
- [ ] Add water-height sampling accuracy tests
- [ ] Add buoyancy determinism tests
- [ ] Add ocean-tiling seam-continuity tests
- [ ] Add flow and current reproducibility tests
- [ ] Add shoreline-blending correctness tests
- [ ] Add save/restore fidelity tests for water and sea state
- [ ] Add performance budget tests for simulation and reflections

# 8 · World Streaming / World Partitioning / Open World

## Automatic World Partitioning & Grid

- [ ] Add automatic spatial partitioning of the world into streaming cells
- [ ] Add a runtime spatial hash grid for fast cell lookup
- [ ] Add automatic assignment of placed content to cells by position
- [ ] Add configurable cell size with a sensible automatic default
- [ ] Add multiple overlapping grids for different content classes
- [ ] Add cell bounds computed from contained content
- [ ] Add loose cells for oversized objects that span boundaries
- [ ] Add always-loaded content that is never streamed out
- [ ] Add automatic re-partitioning when content moves between cells
- [ ] Add incremental background partition rebuild
- [ ] Add per-cell content manifests generated on save
- [ ] Add spatial queries returning the cells overlapping a region
- [ ] Add deterministic cell identifiers stable across edits
- [ ] Add nested and multi-resolution grids for varied content density

## Automatic Streaming

- [ ] Add distance-based automatic streaming with zero manual setup
- [ ] Add load of cells entering the streaming radius
- [ ] Add unload of cells leaving the streaming radius
- [ ] Add hysteresis bands to avoid load/unload thrashing at boundaries
- [ ] Add prefetch of cells in the direction of movement
- [ ] Add priority ordering by distance and view direction
- [ ] Add streaming enabled by default for new worlds
- [ ] Add per-cell load and unload lifecycle events
- [ ] Add smooth activation so content appears without a pop
- [ ] Add a global streaming on/off toggle for debugging
- [ ] Add automatic tuning of the streaming radius from performance headroom
- [ ] Add graceful handling when content loads slower than movement
- [ ] Add configurable per-content-class streaming distances

## Streaming Sources

- [ ] Add the camera as a default streaming source
- [ ] Add player and character streaming sources
- [ ] Add gameplay-defined streaming sources (objectives, spawns)
- [ ] Add per-source streaming radius and priority
- [ ] Add velocity-based predictive streaming per source
- [ ] Add multiple simultaneous sources for split-screen and multiplayer
- [ ] Add temporary streaming sources for teleport and fast travel
- [ ] Add source shapes (sphere, box, along-spline) for shaped streaming
- [ ] Add merging and deduplication of overlapping sources
- [ ] Add priority boosting for the local player source
- [ ] Add streaming-source debug visualization

## Origin Rebasing & Large Coordinates

- [ ] Add a floating-origin system that recenters the world near the camera
- [ ] Add automatic origin shift when the camera exceeds a threshold
- [ ] Add rebasing of rendering transforms on origin shift
- [ ] Add rebasing of physics bodies on origin shift
- [ ] Add rebasing of audio positions on origin shift
- [ ] Add rebasing of particles and effects on origin shift
- [ ] Add double-precision world coordinates for authoritative positions
- [ ] Add conversion between world-space and rebased render-space
- [ ] Add seamless origin shifts with no visible jump
- [ ] Add per-view local origins for multiplayer and split-screen
- [ ] Add precision diagnostics that warn before coordinate error grows
- [ ] Add gameplay-code helpers that hide rebasing from designers
- [ ] Add rebasing hooks for streamed navigation and spatial structures
- [ ] Add rebasing of shadows, reflections, and large-scale effects

## Cell Loading Pipeline

- [ ] Add fully asynchronous cell loading off the main thread
- [ ] Add background IO for cell data
- [ ] Add background deserialization of cell content
- [ ] Add staged GPU upload of streamed meshes and textures
- [ ] Add job-system integration for parallel cell loads
- [ ] Add incremental activation spread across frames to avoid hitches
- [ ] Add cancellation of in-flight loads when a cell leaves range
- [ ] Add load coalescing when a cell is requested multiple times
- [ ] Add retry and error handling for failed cell loads
- [ ] Add placeholder or proxy display while a cell finishes loading
- [ ] Add load-completion callbacks and events
- [ ] Add ordering so dependencies load before dependents

## Hierarchical Distant Proxies

- [ ] Add hierarchical proxy meshes for cells beyond the streaming radius
- [ ] Add automatic generation of distant proxies from cell content
- [ ] Add merged-mesh proxies for static content
- [ ] Add impostor proxies for very distant content
- [ ] Add multi-level proxy hierarchies for continuous distance coverage
- [ ] Add smooth transitions between loaded content and proxies
- [ ] Add automatic proxy rebuild when cell content changes
- [ ] Add proxy material and lighting matching to loaded content
- [ ] Add per-cluster proxy grouping to bound draw counts
- [ ] Add proxy budgets and quality scaling
- [ ] Add proxy debug visualization
- [ ] Add incremental background proxy baking

## Logical Content Layers

- [ ] Add logical content layers independent of spatial cells
- [ ] Add runtime enable and disable of content layers
- [ ] Add layer variants (day/night, pre/post event, difficulty)
- [ ] Add per-layer streaming policy (always, distance, never)
- [ ] Add layer states that swap sets of content
- [ ] Add gameplay-driven layer activation
- [ ] Add save and restore of active layer states
- [ ] Add editor authoring and assignment of content to layers
- [ ] Add layer visualization and isolation in the editor
- [ ] Add validation that layer references resolve
- [ ] Add nested layers and layer groups

## Per-Object Files & Team Workflow

- [ ] Add one-file-per-object storage for streamed content
- [ ] Add automatic file placement by cell and layer
- [ ] Add granular files that avoid source-control merge conflicts
- [ ] Add lazy loading of individual object files
- [ ] Add batch packing of object files for shipping builds
- [ ] Add rename and move handling that preserves references
- [ ] Add per-object dirty tracking for partial saves
- [ ] Add integrity checks across object files in a cell
- [ ] Add a migration path from monolithic to per-object storage
- [ ] Add conflict-free concurrent editing of different cells
- [ ] Add cross-object reference resolution across files

## Instanced Level Chunks

- [ ] Add reusable level chunks that can be instanced across the world
- [ ] Add per-instance overrides on level chunks
- [ ] Add streaming of instanced chunks by distance
- [ ] Add nested chunks within chunks
- [ ] Add stable references into instanced chunk content
- [ ] Add spawn and despawn of chunk instances at runtime
- [ ] Add chunk-instance registries and lookup
- [ ] Add editor placement and editing of chunk instances
- [ ] Add validation for cyclic chunk nesting
- [ ] Add chunk-instance save and restore
- [ ] Add automatic cell assignment for chunk instances

## ECS Integration & Bulk Streaming

- [ ] Stream world content as archetype/chunk storage
- [ ] Add bulk load of entities through the deferred command buffer
- [ ] Add bulk unload and recycling of streamed entities
- [ ] Add chunked serialization and deserialization of streamed cells
- [ ] Add delta snapshots for streamed cell state
- [ ] Add streaming visitors that process cells without full loads
- [ ] Add parallel cell load and build across the worker pool
- [ ] Add zero-copy handoff of streamed data into archetype storage
- [ ] Add stable entity identity across unload and reload
- [ ] Add deterministic entity id remapping on stream-in
- [ ] Add memory-traffic-aware batch sizes for stream-in
- [ ] Add streaming of singleton and world-resource state
- [ ] Add SIMD-friendly layouts preserved across streaming

## Streaming State & Persistence

- [ ] Add persistence of modified streamed entities when they unload
- [ ] Add an offloaded state store for unloaded cells
- [ ] Add dirty tracking so only changed content is persisted
- [ ] Add restore of persisted state when a cell reloads
- [ ] Add distinction between authored content and runtime changes
- [ ] Add save integration that captures streamed and unloaded state
- [ ] Add compaction of the offloaded state store
- [ ] Add versioned migration of persisted streamed state
- [ ] Add per-object persistence policy (persistent, resettable)
- [ ] Add reset of a region to its authored state on demand
- [ ] Add async flush of persisted state without stalls

## Seamless World Travel

- [ ] Add seamless movement across the whole world with no loading screens
- [ ] Add portal and doorway handoff between regions
- [ ] Add fast-travel that pre-streams the destination
- [ ] Add teleport that relocates streaming sources and origin
- [ ] Add background pre-streaming during scripted sequences
- [ ] Add hidden-loading corridors for tight interiors
- [ ] Add smooth handoff between interior and exterior streaming
- [ ] Add a fallback loading transition when streaming cannot keep up
- [ ] Add continuity of audio and lighting across transitions
- [ ] Add validation that no traversal path outruns streaming
- [ ] Add pre-streaming from predicted player intent

## Navigation & Physics Streaming

- [ ] Add streamed navigation-mesh tiles per cell
- [ ] Add stitching of navigation tiles at cell boundaries
- [ ] Add streamed collision per cell
- [ ] Add physics activation and sleep per streamed region
- [ ] Add async navigation and collision build on stream-in
- [ ] Add rebasing of navigation and physics on origin shift
- [ ] Add navigation queries that trigger streaming of needed tiles
- [ ] Add fallback navigation for not-yet-streamed regions
- [ ] Add validation of navigation continuity across cells
- [ ] Add debug visualization of streamed navigation and collision
- [ ] Add long-range path planning across unloaded regions

## Audio & Ambient Streaming

- [ ] Add streamed audio and ambience zones per region
- [ ] Add reverb and acoustic settings streamed per area
- [ ] Add crossfade of ambience across region boundaries
- [ ] Add distance-based streaming of audio sources
- [ ] Add rebasing of audio positions on origin shift
- [ ] Add pre-streaming of audio for fast travel and teleport
- [ ] Add budgets for concurrently streamed audio
- [ ] Add validation of audio-zone coverage and gaps
- [ ] Add debug visualization of audio zones
- [ ] Add handoff of interior and exterior audio

## Streaming Budgets & Throttling

- [ ] Add a memory budget for loaded cells with eviction
- [ ] Add a frame-time budget for activation work
- [ ] Add a bandwidth budget for background IO
- [ ] Add priority queues for load and unload requests
- [ ] Add adaptive throttling from current performance headroom
- [ ] Add pinned cells exempt from eviction
- [ ] Add an eviction policy by distance, recency, and priority
- [ ] Add over-budget diagnostics with responsible cells
- [ ] Add graceful degradation to proxies when over budget
- [ ] Add per-content-class budgets (meshes, textures, entities)
- [ ] Add spike smoothing so large regions load progressively

## Automatic Setup & User-Friendly UX

- [ ] Make world streaming on by default with zero configuration
- [ ] Add automatic cell-size selection from world scale and content density
- [ ] Add automatic streaming-radius selection from performance
- [ ] Add a single toggle to enable or disable streaming
- [ ] Add plain-language presets (small, large, huge open world)
- [ ] Add sensible defaults that just work for new projects
- [ ] Add automatic conversion of an existing world into a streamed world
- [ ] Add guidance and warnings when content is misconfigured for streaming
- [ ] Add a one-click "optimize streaming" analysis and fix
- [ ] Add a beginner mode that hides partition internals
- [ ] Add clear, non-technical status readouts (loaded, loading, budget)
- [ ] Add automatic always-loaded detection for critical systems
- [ ] Add an assistant that suggests fixes for streaming problems

## Editor Tools & Visualization

- [ ] Add a partition grid overlay in the editor
- [ ] Add loaded, loading, and unloaded cell visualization
- [ ] Add a world overview map with cell states
- [ ] Add manual pin and force-load of cells for editing
- [ ] Add per-cell content and memory statistics
- [ ] Add a streaming-source preview and radius display
- [ ] Add simulate-streaming-from-here in the editor
- [ ] Add content-to-cell assignment inspection
- [ ] Add layer isolation and toggling in the editor
- [ ] Add a proxy vs full-content compare view
- [ ] Add jump-to-cell navigation
- [ ] Add warnings for content spanning too many cells

## Large Tiled Worlds

- [ ] Add large tiled worlds composed of many regions
- [ ] Add per-tile bounds and position in world space
- [ ] Add distance-based tile streaming
- [ ] Add tiled terrain and content aligned to the partition
- [ ] Add a world overview for composing and arranging tiles
- [ ] Add automatic alignment and seam matching between tiles
- [ ] Add per-tile origin offsets for large coordinate ranges
- [ ] Add import and assembly of tiles into one world
- [ ] Add tile-level enable/disable and variants
- [ ] Add validation of tile coverage and overlaps

## Networking & Multiplayer Streaming

- [ ] Add server-authoritative streaming decisions
- [ ] Add per-client relevance and streaming radius
- [ ] Add replication of streamed and offloaded state
- [ ] Add per-client independent origins and rebasing
- [ ] Add consistent cell identity across server and clients
- [ ] Add prioritized streaming around each connected player
- [ ] Add spawn and despawn replication tied to cell lifecycle
- [ ] Add bandwidth-aware streaming for networked sessions
- [ ] Add deterministic streaming for lockstep and replay
- [ ] Add validation of client and server streamed-state consistency

## Performance & Diagnostics

- [ ] Add streaming statistics (loaded cells, pending, memory, bandwidth)
- [ ] Add hitch detection attributed to streaming work
- [ ] Add per-cell load-time profiling
- [ ] Add residency and eviction telemetry
- [ ] Add a streaming timeline for load and unload events
- [ ] Add memory-budget and over-budget reporting
- [ ] Add warnings when streaming cannot keep up with movement
- [ ] Add a headless streaming benchmark harness
- [ ] Add machine-readable streaming metrics for CI
- [ ] Add a live streaming HUD for profiling

## Testing & Validation

- [ ] Add streaming determinism tests for a fixed traversal path
- [ ] Add origin-rebasing correctness and precision tests
- [ ] Add cell load and unload lifecycle tests
- [ ] Add seam-continuity tests across cell boundaries
- [ ] Add persisted-state save and restore fidelity tests
- [ ] Add memory-budget and eviction stress tests
- [ ] Add fast-travel and teleport pre-streaming tests
- [ ] Add navigation and physics streaming continuity tests
- [ ] Add proxy-to-content transition tests
- [ ] Add a large-world traversal soak test

# 9 · LOD Management

## LOD System Core & Data Model

- [ ] Add a level-of-detail chain per mesh with ordered levels
- [ ] Add per-level mesh geometry references
- [ ] Add per-level material assignments
- [ ] Add screen-size thresholds per level
- [ ] Add distance thresholds as an alternative selection metric
- [ ] Add per-level bounds and error metadata
- [ ] Add a shared LOD-settings asset reusable across meshes
- [ ] Add a lowest-level fallback (billboard or single quad)
- [ ] Add per-level triangle and vertex counts stored for budgeting
- [ ] Add a versioned LOD asset format with migration
- [ ] Add LOD data shared between mesh instances
- [ ] Add validation that LOD chains are complete and monotonic
- [ ] Add a unified LOD interface across meshes, terrain, and instances

## Automatic Mesh LOD Generation

- [ ] Add automatic mesh simplification generating LOD levels
- [ ] Add target-triangle-count reduction
- [ ] Add target-percentage reduction
- [ ] Add a view-independent geometric error metric
- [ ] Add silhouette and boundary preservation
- [ ] Add UV-seam preservation
- [ ] Add normal and hard-edge preservation
- [ ] Add vertex-color and attribute preservation
- [ ] Add skinning-weight preservation for skinned meshes
- [ ] Add material-boundary preservation during reduction
- [ ] Add automatic material and texture reduction at lower levels
- [ ] Add automatic LOD generation on asset import
- [ ] Add per-level reduction overrides
- [ ] Add regeneration when the source mesh changes
- [ ] Add batch LOD generation across many assets
- [ ] Add reduction quality presets

## LOD Selection & Switching

- [ ] Add screen-coverage-based LOD selection
- [ ] Add distance-based LOD selection
- [ ] Add hysteresis to prevent switching flicker at thresholds
- [ ] Add a global LOD bias control
- [ ] Add per-object LOD bias and forced LOD
- [ ] Add per-view LOD selection (main, shadow, reflection)
- [ ] Add GPU-side LOD selection for instanced content
- [ ] Add field-of-view-aware screen-size computation
- [ ] Add resolution-aware selection so LOD scales with output size
- [ ] Add priority so important objects hold higher LOD
- [ ] Add SIMD-vectorized LOD selection over instance chunks
- [ ] Add per-object selection debug output
- [ ] Add deterministic selection for replay and tests

## LOD Transitions & Blending

- [ ] Add dithered cross-fade between LOD levels
- [ ] Add alpha-blended transitions as an option
- [ ] Add geometric vertex morphing between levels
- [ ] Add screen-door transition for opaque content
- [ ] Add temporal transitions spread across frames
- [ ] Add transition distance and duration controls
- [ ] Add pop-free switching validation
- [ ] Add transition suppression for fast-moving or distant objects
- [ ] Add overdraw control during blended transitions
- [ ] Add per-object transition-style overrides
- [ ] Add transition debug visualization

## Impostors & Billboards

- [ ] Add octahedral impostor capture for distant meshes
- [ ] Add octahedral impostor rendering
- [ ] Add billboard and cross-quad far LOD
- [ ] Add automatic impostor generation from source meshes
- [ ] Add impostor atlas packing
- [ ] Add impostor lighting that responds to scene light direction
- [ ] Add impostor normal and depth capture for parallax
- [ ] Add impostor as the automatic last LOD level
- [ ] Add impostor resolution and quality controls
- [ ] Add smooth blend from mesh LOD to impostor
- [ ] Add impostor regeneration when the mesh changes
- [ ] Add impostor memory budgets

## Virtualized Cluster Geometry

- [ ] Add cluster-based continuous geometry LOD
- [ ] Add automatic cluster-hierarchy generation from source meshes
- [ ] Add per-cluster bounds and error for selection
- [ ] Add view-dependent cluster LOD selection at pixel scale
- [ ] Add GPU per-cluster culling and LOD
- [ ] Add streaming of geometry clusters by need
- [ ] Add seamless cluster-boundary LOD without cracks
- [ ] Add material support across clustered geometry
- [ ] Add shadow rendering from clustered geometry
- [ ] Add a fallback traditional-LOD path for unsupported hardware
- [ ] Add memory and streaming budgets for clustered geometry
- [ ] Add cluster-level statistics and diagnostics
- [ ] Add an import and build pipeline for clustered assets
- [ ] Add quality scaling of cluster detail

## Terrain LOD

- [ ] Add continuous level-of-detail for the terrain surface
- [ ] Add a quadtree tile LOD hierarchy
- [ ] Add a clipmap-style camera-relative terrain LOD
- [ ] Add screen-space geometric error driving tile subdivision
- [ ] Add geomorphing between terrain LOD levels
- [ ] Add seamless stitching between adjacent tile LODs
- [ ] Add skirts or edge fans to hide LOD cracks
- [ ] Add tessellation-based terrain LOD where supported
- [ ] Add a mesh-LOD fallback where tessellation is unavailable
- [ ] Add height-based detail refinement near the camera
- [ ] Add per-tile LOD budgets and triangle limits
- [ ] Add hole-aware terrain LOD
- [ ] Add collision LOD separate from render LOD for terrain
- [ ] Add terrain LOD transitions without visible popping
- [ ] Add terrain LOD debug visualization (rings, tile levels)
- [ ] Add deterministic terrain LOD for tests

## Terrain Material & Detail LOD

- [ ] Add distance-based blending of terrain material layers
- [ ] Add macro-material substitution at far distance
- [ ] Add splat-weight LOD to reduce sampling far away
- [ ] Add normal-map and detail-map fade with distance
- [ ] Add a triplanar-to-simple projection switch at distance
- [ ] Add procedural detail meshes (rocks, tufts) with their own LOD
- [ ] Add layer-count reduction at lower detail
- [ ] Add far-distance baked color and normal for terrain
- [ ] Add a seamless transition between detail and macro material
- [ ] Add terrain material LOD quality presets

## Hierarchical Merged LOD

- [ ] Add merging of groups of static meshes into combined distant proxies
- [ ] Add automatic generation of merged proxies
- [ ] Add material atlasing for merged proxies
- [ ] Add multi-level merged hierarchies for continuous coverage
- [ ] Add cluster grouping to bound proxy draw counts
- [ ] Add automatic proxy rebuild when source content changes
- [ ] Add smooth transition between individual meshes and merged proxies
- [ ] Add proxy generation as an offline or background bake
- [ ] Add integration with world streaming for distant regions
- [ ] Add merged-proxy budgets and diagnostics
- [ ] Add merged-proxy debug visualization

## Skinned & Animated LOD

- [ ] Add geometry LOD for skinned meshes
- [ ] Add bone-count reduction per LOD level
- [ ] Add animation update-rate reduction with distance
- [ ] Add skinning-quality reduction at lower levels
- [ ] Add morph-target reduction per LOD
- [ ] Add off-screen and distant animation pausing
- [ ] Add interpolation to hide reduced update rates
- [ ] Add crowd-friendly aggressive LOD for many characters
- [ ] Add per-character LOD bias and forced LOD
- [ ] Add validation of skinning correctness across LODs
- [ ] Add skinned-LOD debug visualization

## Instanced & Foliage LOD Management

- [ ] Add centrally managed per-instance LOD selection
- [ ] Add density reduction as a distance LOD for instances
- [ ] Add merged distant meshes for instanced content
- [ ] Add impostor last-LOD for instances
- [ ] Add SIMD-vectorized instance LOD selection
- [ ] Add per-instance-type LOD distances and budgets
- [ ] Add shadow LOD independent from render LOD for instances
- [ ] Add cross-fade transitions for instanced LOD
- [ ] Add GPU-driven instance LOD selection
- [ ] Add unified LOD settings shared with the foliage system
- [ ] Add instance LOD debug visualization

## Material & Shader LOD

- [ ] Add shader-complexity reduction at distance
- [ ] Add feature dropping far away (parallax, detail layers, clearcoat)
- [ ] Add texture-mip-driven material simplification
- [ ] Add a reduced lighting model for distant objects
- [ ] Add material-layer-count reduction at lower LODs
- [ ] Add a cheap far-distance material variant per material
- [ ] Add automatic shader-LOD variant generation
- [ ] Add per-quality-tier material LOD mapping
- [ ] Add validation that material LODs match visually
- [ ] Add material-LOD debug visualization

## LOD Streaming

- [ ] Add on-demand streaming of individual LOD levels
- [ ] Add loading of higher detail as objects approach
- [ ] Add eviction of unused high-detail LODs under memory pressure
- [ ] Add per-LOD memory budgets
- [ ] Add prioritized LOD streaming by screen coverage
- [ ] Add placeholder lower-LOD display while higher LOD loads
- [ ] Add async LOD load off the main thread
- [ ] Add coalescing of duplicate LOD load requests
- [ ] Add integration with world and asset streaming
- [ ] Add LOD residency diagnostics

## LOD Budgets & Quality Scaling

- [ ] Add a global triangle budget with automatic LOD bias
- [ ] Add a draw-call budget influencing LOD and merging
- [ ] Add quality presets (draft, standard, high, ultra)
- [ ] Add per-platform LOD bias and caps
- [ ] Add dynamic LOD bias driven by current framerate
- [ ] Add per-category budgets (characters, props, terrain, foliage)
- [ ] Add priority protection so key objects resist budget cuts
- [ ] Add smooth budget-driven bias changes to avoid popping
- [ ] Add over-budget diagnostics with responsible objects
- [ ] Add a user-facing quality slider mapped to LOD scaling
- [ ] Add budget reporting in stats

## Automatic Setup & User-Friendly UX

- [ ] Generate LODs automatically on import with good defaults
- [ ] Add a single toggle to enable automatic LOD
- [ ] Add automatic screen-size threshold selection
- [ ] Add plain-language presets (fewer details, balanced, high detail)
- [ ] Add sensible defaults that just work without tuning
- [ ] Add a one-click "optimize LODs" analysis and fix
- [ ] Add automatic detection of objects that need aggressive LOD
- [ ] Add guidance and warnings for missing or poor LODs
- [ ] Add a beginner mode that hides reduction internals
- [ ] Add non-technical status readouts (current detail, savings)
- [ ] Add automatic terrain LOD with no manual setup
- [ ] Add consistent, reversible LOD settings across all content

## Authoring Tools

- [ ] Add a LOD editor with per-level preview
- [ ] Add manual import of authored LOD meshes
- [ ] Add per-level reduction-setting controls
- [ ] Add per-level material assignment
- [ ] Add threshold tuning with live preview
- [ ] Add side-by-side comparison of LOD levels
- [ ] Add a wireframe and triangle-count view per level
- [ ] Add copy and reuse of LOD settings across assets
- [ ] Add mixing of authored and generated LOD levels
- [ ] Add a preview of transitions at chosen distances
- [ ] Add batch editing of LOD settings across a selection

## Editor Visualization & Debugging

- [ ] Add a LOD-level color heatmap overlay
- [ ] Add a current-LOD-per-object overlay
- [ ] Add LOD distance rings around the camera
- [ ] Add a forced-LOD view mode
- [ ] Add a triangle and draw-call HUD
- [ ] Add a transition-in-progress visualization
- [ ] Add terrain LOD tile-level visualization
- [ ] Add a per-object LOD inspector
- [ ] Add highlighting of objects missing LODs
- [ ] Add an overdraw view for transition tuning
- [ ] Add a screenshot-friendly clean LOD overlay

## Performance & Diagnostics

- [ ] Add LOD statistics (levels active, triangles, draw calls)
- [ ] Add per-object and per-category triangle counts
- [ ] Add LOD-switch frequency and cost profiling
- [ ] Add transition overdraw profiling
- [ ] Add memory reporting per LOD level
- [ ] Add warnings when LOD fails to hit budgets
- [ ] Add a headless LOD benchmark harness
- [ ] Add machine-readable LOD metrics for CI
- [ ] Add a live LOD HUD for profiling
- [ ] Add attribution of frame cost to LOD categories

## Testing & Validation

- [ ] Add mesh-simplification error and quality tests
- [ ] Add attribute-preservation tests (UVs, normals, weights)
- [ ] Add LOD-selection determinism tests
- [ ] Add transition correctness and pop-free tests
- [ ] Add terrain LOD seam-continuity tests
- [ ] Add terrain geomorph correctness tests
- [ ] Add impostor generation and rendering tests
- [ ] Add clustered-geometry LOD tests
- [ ] Add budget and quality-scaling tests
- [ ] Add golden-image tests across LOD levels
- [ ] Add large-scene LOD performance stress tests

# 10 · Culling / Visibility / Occlusion

## Visibility System Core & Data Model

- [ ] Add a visibility state per renderable object
- [ ] Add per-view visibility results with visible and culled sets
- [ ] Add visibility flags (never cull, always cull, force visible)
- [ ] Add cached bounds (sphere and box) per object
- [ ] Add visibility groups for shared culling policy
- [ ] Add per-object relevance flags per pass (base, shadow, depth, custom)
- [ ] Add dirty tracking so only moved objects are re-evaluated
- [ ] Add stable object handles for cross-frame visibility coherence
- [ ] Add a visibility result buffer consumable by the renderer
- [ ] Add per-object skip reasons for debugging
- [ ] Add a unified visibility interface across meshes, terrain, and instances

## Bounds & Spatial Acceleration

- [ ] Add a broad-phase spatial acceleration structure for the scene
- [ ] Add a bounding-volume hierarchy for static content
- [ ] Add surface-area-heuristic BVH construction
- [ ] Add a fast linear BVH build from Morton codes for dynamic content
- [ ] Add binned BVH construction for build-quality control
- [ ] Add a two-level structure separating static and dynamic content
- [ ] Add a loose octree option for clustered dynamic content
- [ ] Add a uniform grid and spatial hash option for even distributions
- [ ] Add incremental insertion and removal
- [ ] Add refit of parent bounds when leaves move
- [ ] Add periodic rebuild scheduling when quality degrades
- [ ] Add parallel construction across the worker pool
- [ ] Add automatic bounds computation from geometry
- [ ] Add tight bounds recomputation for animated and skinned meshes
- [ ] Add merged group bounds for hierarchical culling
- [ ] Add cache-friendly node layout for traversal
- [ ] Add a compact quantized node format
- [ ] Add region and range queries
- [ ] Add ray queries for gameplay and picking
- [ ] Add sphere and box overlap queries
- [ ] Add nearest and k-nearest queries
- [ ] Add tree-quality metrics and rebuild heuristics
- [ ] Add memory budgets and diagnostics for the structure
- [ ] Add debug visualization of the structure and its bounds

## Frustum Culling

- [ ] Add view-frustum plane extraction from the view-projection matrix
- [ ] Add sphere-versus-frustum tests
- [ ] Add box-versus-frustum tests
- [ ] Add hierarchical frustum culling over the spatial structure
- [ ] Add early-accept for fully-inside nodes to skip subtrees
- [ ] Add SIMD-vectorized batch frustum tests
- [ ] Add per-view frustums for every active view
- [ ] Add near-plane and far-plane distance culling
- [ ] Add split frustums for shadow cascades
- [ ] Add oblique and mirrored frustums for reflections
- [ ] Add conservative tests to avoid false culling at edges
- [ ] Add frustum-culling debug visualization

## Distance & Contribution Culling

- [ ] Add a global maximum draw distance
- [ ] Add per-object cull distance
- [ ] Add cull-distance volumes that set distances by region
- [ ] Add size-on-screen thresholds for small-object culling
- [ ] Add minimum screen-coverage culling
- [ ] Add per-category distance policies (props, foliage, effects)
- [ ] Add distance-based fade-out before culling to avoid popping
- [ ] Add importance and priority overrides for key objects
- [ ] Add resolution-aware screen-size computation
- [ ] Add automatic distance tuning from performance headroom
- [ ] Add contribution-culling debug visualization

## GPU Occlusion Culling

- [ ] Build a hierarchical depth pyramid from the scene depth
- [ ] Downsample with conservative farthest-depth reduction
- [ ] Handle non-power-of-two depth with correct mip coverage
- [ ] Project object bounds to a screen-space min/max rectangle
- [ ] Select the mip level that covers the bounds rectangle in a few texels
- [ ] Compare object nearest depth against the sampled far depth
- [ ] Add a two-pass pipeline that draws last-frame visibles, rebuilds the pyramid, then tests the rest
- [ ] Draw newly-appeared objects in the second pass
- [ ] Seed occlusion from previous-frame depth reprojected by camera motion
- [ ] Handle the first frame and camera cuts without over-culling
- [ ] Add conservative bounds expansion to avoid edge popping
- [ ] Maintain a false-negative list of disoccluded objects for re-test
- [ ] Re-draw recovered objects the next frame without a visible gap
- [ ] Add per-cluster occlusion tests for clustered geometry
- [ ] Add per-meshlet occlusion tests
- [ ] Build a separate hierarchical depth per shadow view
- [ ] Add occlusion culling for reflection and planar views
- [ ] Add occlusion culling within cascaded shadow maps
- [ ] Support masked and alpha-tested occluders in the depth pyramid
- [ ] Exclude transparent objects from occluder contribution
- [ ] Add subpixel-safe tests for thin and small objects
- [ ] Tighten occludee bounds toward the geometry silhouette where affordable
- [ ] Output a GPU visibility bitfield consumable by draw generation
- [ ] Add stream compaction of survivors after occlusion
- [ ] Add occlusion-result readback for statistics with a frame delay
- [ ] Add a toggle between reprojection and two-pass modes
- [ ] Add temporal hysteresis so flickering occludees do not thrash
- [ ] Never occlude objects larger than the screen
- [ ] Add always-visible tagging that bypasses occlusion
- [ ] Add a conservative depth bias trading accuracy for safety
- [ ] Add hierarchical-depth and per-object occlusion debug views
- [ ] Add validation that occlusion never hides a truly visible object

## Hardware Occlusion Queries

- [ ] Add hardware occlusion queries for coarse per-object tests
- [ ] Add bounding-proxy rendering for query submission
- [ ] Add asynchronous query result retrieval to hide latency
- [ ] Add round-robin scheduling that amortizes queries across frames
- [ ] Add predicated rendering where the backend supports it
- [ ] Add last-known-result reuse while a query is pending
- [ ] Add conservative assume-visible on missing results
- [ ] Add batching of queries for grouped objects
- [ ] Add a capability check and fallback when queries are unavailable
- [ ] Add query-cost diagnostics and limits

## Software Occlusion Culling

- [ ] Add a CPU depth rasterizer for occluders
- [ ] Add hierarchical coverage tiles for fast rejection
- [ ] Add automatic and manual occluder selection
- [ ] Add conservative depth to avoid over-culling
- [ ] Add multi-threaded occluder rasterization
- [ ] Add per-object tests against the software depth
- [ ] Add a budget on occluders rasterized per frame
- [ ] Add merging with GPU occlusion results
- [ ] Add a fallback path where GPU occlusion is unavailable
- [ ] Add software-occlusion debug visualization

## Precomputed Visibility

- [ ] Add a cell subdivision of the scene for precomputed visibility
- [ ] Add sampling points per cell for visibility rays
- [ ] Add ray-cast sampling to determine cell-to-object visibility
- [ ] Add portal-based exact visibility computation for interiors
- [ ] Add a conservative visibility mode that never hides visible objects
- [ ] Add an aggressive mode with a tunable error tolerance
- [ ] Add an offline visibility bake step
- [ ] Add distributed and incremental baking of changed regions
- [ ] Add a memory-compact visibility-set representation
- [ ] Add compression of potentially-visible sets
- [ ] Add runtime lookup of the visible set from the camera cell
- [ ] Add streaming of visibility data with the world
- [ ] Add merging of precomputed visibility with runtime culling
- [ ] Add validation that precomputed sets never hide visible objects
- [ ] Add precomputed-visibility debug visualization

## Portals & Cells

- [ ] Add a portal-and-cell graph for interiors
- [ ] Add cell membership assignment for objects
- [ ] Add portal traversal culling from the camera cell
- [ ] Add portal frustum narrowing through each opening
- [ ] Add recursive traversal with visited-cell tracking
- [ ] Add antiportals for large blocking occluders
- [ ] Add door and window portals that open and close
- [ ] Add interior-to-exterior portal handoff
- [ ] Add mirror and view portals for reflections and see-through
- [ ] Add automatic cell and portal generation helpers
- [ ] Add portal-graph authoring and editing tools
- [ ] Add portal and cell debug visualization

## Occluder Authoring

- [ ] Add occluder meshes and volumes
- [ ] Add automatic simplified-occluder generation from geometry
- [ ] Add occluder and occludee flags per object
- [ ] Add box, plane, and volume occluder primitives
- [ ] Add occluder quality and budget controls
- [ ] Add terrain as an automatic occluder
- [ ] Add large static meshes as automatic occluders
- [ ] Add occluder validation for watertightness and size
- [ ] Add occluder authoring and preview tools
- [ ] Add occluder debug visualization

## GPU-Driven Culling

- [ ] Add a GPU scene description of instances, bounds, and materials
- [ ] Add persistent GPU buffers updated incrementally each frame
- [ ] Add compute-shader frustum culling of all instances
- [ ] Add compute-shader distance and contribution culling
- [ ] Add compute-shader occlusion culling of all instances
- [ ] Add per-cluster frustum culling
- [ ] Add per-cluster cone (backface) culling
- [ ] Add per-cluster occlusion culling
- [ ] Add per-meshlet culling for a mesh-shader path
- [ ] Add a task/amplification stage that expands visible work
- [ ] Add stream compaction of survivors into dense draw lists
- [ ] Add indirect draw-argument generation on the GPU
- [ ] Add multi-draw-indirect submission from generated arguments
- [ ] Add per-view GPU culling for main, shadow, and reflection passes
- [ ] Add sorting and batching of survivors by pipeline and material
- [ ] Add two-level culling that rejects whole clusters before instances
- [ ] Add a visibility bitfield shared across passes within a frame
- [ ] Add persistent-thread and wave-efficient culling kernels
- [ ] Add a mesh-shader path and a fallback vertex path
- [ ] Add a full CPU fallback where compute or indirect is unavailable
- [ ] Add GPU-culling counters and readback for diagnostics
- [ ] Add deterministic GPU culling for tests and replay
- [ ] Add per-instance LOD selection fused into the culling pass
- [ ] Add shadow-caster expansion handled on the GPU
- [ ] Add GPU-culling debug visualization

## Per-View & Multi-View Culling

- [ ] Add independent culling per active view
- [ ] Add shared broad-phase results reused across views
- [ ] Add culling for shadow views and cascades
- [ ] Add culling for reflection and planar views
- [ ] Add culling for multiple cameras and split-screen
- [ ] Add view-relevance flags to skip irrelevant passes
- [ ] Add merged culling for views sharing a frustum region
- [ ] Add per-view budgets and priorities
- [ ] Add cross-view visibility caching where valid
- [ ] Add a combined-frustum pre-pass across all shadow cascades
- [ ] Add per-view culling statistics

## Shadow-Caster Culling

- [ ] Add culling of shadow casters per light
- [ ] Add culling of casters per shadow cascade
- [ ] Add caster culling by shadow-frustum bounds
- [ ] Add culling of casters that cannot affect visible receivers
- [ ] Add extrusion of caster bounds along the light direction
- [ ] Add distance and size culling for shadow casters
- [ ] Add occlusion culling within shadow views
- [ ] Add merged caster culling across cascades
- [ ] Add a receiver-region test to bound relevant casters
- [ ] Add shadow-caster culling statistics
- [ ] Add shadow-caster culling debug visualization

## Backface & Cluster Cone Culling

- [ ] Add backface culling configuration per material
- [ ] Add two-sided and double-sided handling
- [ ] Add meshlet and cluster cone culling
- [ ] Add per-cluster normal-cone data generation
- [ ] Add degenerate and zero-area triangle rejection
- [ ] Add orientation-aware culling for instanced content
- [ ] Add cone-culling integration with GPU-driven culling
- [ ] Add cone-culling correctness validation
- [ ] Add cone-culling debug visualization

## Ray-Traced & Distance-Field Visibility

- [ ] Add signed-distance-field proxies for coarse occlusion
- [ ] Add cone or ray tracing against distance fields for visibility
- [ ] Add a global distance field assembled from object fields
- [ ] Add ray-traced occlusion where hardware ray tracing is available
- [ ] Add distance-field occlusion as a fallback without ray tracing
- [ ] Add distance-field-based long-range visibility for streaming
- [ ] Add caching and temporal reuse of ray-traced visibility
- [ ] Add quality and cost controls for ray-traced visibility
- [ ] Add validation against rasterized occlusion results
- [ ] Add distance-field and ray-visibility debug visualization

## Temporal & Predictive Visibility

- [ ] Add temporal coherence reuse of last-frame visibility
- [ ] Add camera-cut detection that invalidates temporal data
- [ ] Add predictive visibility from camera velocity
- [ ] Add latency hiding by prefetching soon-visible content
- [ ] Add disocclusion detection and recovery
- [ ] Add round-robin re-testing to amortize occlusion cost
- [ ] Add temporal smoothing of fade-in and fade-out
- [ ] Add a bounded staleness so stale visibility is refreshed
- [ ] Add validation that temporal reuse never hides visible objects
- [ ] Add temporal-visibility statistics
- [ ] Add temporal-visibility debug visualization

## Foliage & Instance Culling

- [ ] Add per-instance frustum culling
- [ ] Add cluster and cell culling before per-instance tests
- [ ] Add per-instance occlusion culling
- [ ] Add density and contribution culling for instances
- [ ] Add SIMD-vectorized instance culling over chunks
- [ ] Add GPU-driven instance culling
- [ ] Add per-view instance culling for shadows and reflections
- [ ] Add shared culling policy with the foliage system
- [ ] Add instance-culling statistics
- [ ] Add instance-culling debug visualization

## Visibility Queries & Gameplay

- [ ] Add an is-visible query for a given object and view
- [ ] Add line-of-sight queries between points
- [ ] Add visibility-enter and visibility-leave events
- [ ] Add on-screen and off-screen notifications for gameplay
- [ ] Add relevance queries for AI perception and streaming
- [ ] Add coarse gameplay occlusion checks decoupled from rendering
- [ ] Add batched visibility queries for many agents
- [ ] Add distance and angle visibility helpers
- [ ] Add scripting API for visibility queries and events
- [ ] Add deterministic query results for replay and tests

## ECS Integration & Bulk Culling

- [ ] Run culling over archetype chunks in a data-oriented layout
- [ ] Add SIMD frustum-culling kernels over chunks
- [ ] Add SIMD distance and contribution kernels over chunks
- [ ] Add parallel culling across the worker pool
- [ ] Add job-graph scheduling of broad-phase, frustum, and occlusion stages
- [ ] Add bulk visibility-result writes to packed buffers
- [ ] Add cache-friendly bounds layouts for culling
- [ ] Add zero-copy handoff of survivors to GPU draw lists
- [ ] Add memory-traffic-aware batch sizes for culling
- [ ] Add deterministic parallel culling stable across thread counts
- [ ] Add scaling to millions of cullable objects within budget
- [ ] Add incremental culling that reuses results for static objects

## Budgets, Quality & Scaling

- [ ] Add a per-frame culling time budget
- [ ] Add quality scaling of occlusion accuracy
- [ ] Add adaptive occluder and instance limits from headroom
- [ ] Add per-platform culling feature selection
- [ ] Add fallbacks when advanced culling is unavailable
- [ ] Add graceful degradation under heavy load
- [ ] Add priority so key objects are never wrongly culled
- [ ] Add budget over-run diagnostics
- [ ] Add a quality-preset mapping for culling
- [ ] Add culling-cost reporting in stats

## Editor Tools & Visualization

- [ ] Add a frozen-frustum mode to inspect culling from a fixed view
- [ ] Add a culling-statistics overlay
- [ ] Add per-object culling-reason display
- [ ] Add hierarchical-depth and occlusion visualization
- [ ] Add spatial-structure and bounds visualization
- [ ] Add occluder and portal visualization
- [ ] Add a visible-set highlight and culled-set dimming
- [ ] Add distance-ring and cull-distance visualization
- [ ] Add a false-culling detector that flags disappearing objects
- [ ] Add a per-view culling inspector
- [ ] Add a screenshot-friendly clean culling overlay
- [ ] Add a step-through of culling stages for a captured frame

## Performance & Diagnostics

- [ ] Add culling statistics (tested, culled, visible per stage)
- [ ] Add per-stage culling timing
- [ ] Add false-positive and false-negative tracking
- [ ] Add per-view and per-pass culling breakdowns
- [ ] Add occlusion-query and readback latency reporting
- [ ] Add a headless culling benchmark harness
- [ ] Add machine-readable culling metrics for CI
- [ ] Add a live culling HUD for profiling
- [ ] Add attribution of frame cost to culling stages
- [ ] Add warnings when culling exceeds its budget

## Testing & Validation

- [ ] Add frustum-culling correctness tests
- [ ] Add occlusion-culling no-false-culling tests
- [ ] Add culling determinism tests for a fixed view path
- [ ] Add precomputed-visibility correctness tests
- [ ] Add portal-and-cell traversal tests
- [ ] Add shadow-caster culling correctness tests
- [ ] Add per-instance culling correctness tests
- [ ] Add temporal-reuse safety tests
- [ ] Add GPU-versus-CPU culling parity tests
- [ ] Add large-scene culling performance stress tests
- [ ] Add golden-image tests comparing culled and reference renders

# 11 · Level / Scene Management

## Scene Data Model & Format

- [ ] Add a scene asset holding a root hierarchy and entity list
- [ ] Add serialized component data per entity
- [ ] Add stable per-entity identifiers unique within a scene
- [ ] Add global identifiers for cross-scene references
- [ ] Add a scene dependency manifest (assets, other scenes, prefabs)
- [ ] Add per-scene settings (lighting, environment, physics, post-process)
- [ ] Add a distinction between embedded and referenced content
- [ ] Add scene bounds computed from content
- [ ] Add scene metadata (name, description, tags, author, timestamps)
- [ ] Add a binary scene format for fast loading
- [ ] Add a diff-friendly text scene format for source control
- [ ] Add conversion between binary and text formats
- [ ] Add a versioned scene format with migration
- [ ] Add integrity checksums and validation on load
- [ ] Add forward-compatible handling of unknown fields
- [ ] Add per-scene content manifests generated on save
- [ ] Add references from a scene to the assets it uses
- [ ] Add a stable reference type surviving rename and move

## Scene Loading Pipeline

- [ ] Add synchronous scene loading
- [ ] Add fully asynchronous loading off the main thread
- [ ] Add progress reporting with weighted stages
- [ ] Add dependency resolution and preloading before instantiation
- [ ] Add staged instantiation spread across frames to avoid hitches
- [ ] Add loading into a fresh world
- [ ] Add loading additively into the current world
- [ ] Add background deserialization of scene content
- [ ] Add staged GPU upload of scene assets
- [ ] Add job-system integration for parallel load work
- [ ] Add load priorities and queue ordering
- [ ] Add cancellation of in-flight loads
- [ ] Add load coalescing for duplicate requests
- [ ] Add retry and error handling for failed loads
- [ ] Add load-completion callbacks and events
- [ ] Add a placeholder or loading state while content instantiates
- [ ] Add hot-reload of a scene edited on disk
- [ ] Add deterministic load ordering for reproducibility
- [ ] Add a preload API that warms assets without instantiating
- [ ] Add memory-budget-aware loading

## Scene Unloading

- [ ] Add unloading of a loaded scene
- [ ] Add safe destruction of a scene's entities
- [ ] Add release of assets no longer referenced by any scene
- [ ] Add reference counting so shared assets survive
- [ ] Add deferred unload to the end of the frame
- [ ] Add unload events and completion callbacks
- [ ] Add partial unload of a region or layer within a scene
- [ ] Add cleanup validation that no dangling references remain
- [ ] Add async unload without frame stalls

## Multi-Scene & Composition

- [ ] Add additive loading of multiple scenes at once
- [ ] Add an active-scene concept for where new content is created
- [ ] Add moving entities between loaded scenes
- [ ] Add cross-scene references resolved as scenes load and unload
- [ ] Add a master/composition scene that references child scenes
- [ ] Add per-scene enable and disable
- [ ] Add scene ownership of the entities it created
- [ ] Add merging of several scenes into one
- [ ] Add splitting a scene into multiple scenes
- [ ] Add ordering and priority among loaded scenes
- [ ] Add conflict handling for identifiers across additive scenes
- [ ] Add a query for which scene an entity belongs to
- [ ] Add persistence of the active loaded-scene set
- [ ] Add lazy resolution of references to not-yet-loaded scenes

## Scene Lifecycle & Transitions

- [ ] Add a transition manager coordinating unload and load
- [ ] Add optional loading screens during transitions
- [ ] Add fade-out and fade-in transitions
- [ ] Add async pre-load of the destination before activation
- [ ] Add seamless handoff with no loading screen where possible
- [ ] Add persistent objects that survive a scene transition
- [ ] Add a minimum-display-time for loading screens
- [ ] Add cancelable and interruptible transitions
- [ ] Add transition events (started, progress, finished)
- [ ] Add ordered sequencing (fade, unload old, load new, fade back)
- [ ] Add a startup and splash flow before the first scene
- [ ] Add carry-over of state across transitions
- [ ] Add a fallback loading screen when streaming cannot keep up
- [ ] Add validation that transitions release the previous scene

## Sub-Scenes & Nested Scenes

- [ ] Add nested scene references embedded in a parent scene
- [ ] Add independent streaming of sub-scenes
- [ ] Add per-instance overrides on nested scenes
- [ ] Add lifecycle propagation to nested scenes
- [ ] Add cycle detection for nested scene references
- [ ] Add editing a nested scene in isolation
- [ ] Add depth limits and diagnostics for deep nesting
- [ ] Add stable references into nested scene content

## Editor Scene Management

- [ ] Add opening multiple scenes in the editor at once
- [ ] Add a scene tab or list for switching between open scenes
- [ ] Add selecting the active scene for new content
- [ ] Add a per-scene hierarchy panel
- [ ] Add drag-and-drop of entities between open scenes
- [ ] Add per-scene dirty tracking
- [ ] Add save, save-as, and save-all
- [ ] Add unsaved-changes prompts on close
- [ ] Add isolate or solo of a single scene in the viewport
- [ ] Add show and hide of individual open scenes
- [ ] Add a recent-scenes list
- [ ] Add close and reorder of open scenes
- [ ] Add creating a new empty scene
- [ ] Add duplicating an existing scene
- [ ] Add renaming a scene with reference fixup
- [ ] Add per-scene lock to prevent accidental edits
- [ ] Add indication of which scene owns the current selection
- [ ] Add reload-from-disk of a scene discarding edits

## Play Mode & Simulation

- [ ] Add entering and exiting play mode from the editor
- [ ] Add snapshotting the world before play begins
- [ ] Add restoring the exact pre-play state on exit
- [ ] Add play starting from the current scene
- [ ] Add play starting from the configured bootstrap scene
- [ ] Add pause and single-frame step in play mode
- [ ] Add isolation so play-mode changes never touch saved scenes
- [ ] Add a fast enter-play path that avoids a full reload
- [ ] Add a policy for edits made during play
- [ ] Add a deterministic world reset between play sessions
- [ ] Add simulate mode without possessing a player
- [ ] Add capture of play-mode state into a new scene

## Bootstrapping & Game Flow

- [ ] Add a configurable startup scene
- [ ] Add a boot sequence that runs before gameplay
- [ ] Add a persistent bootstrap scene that stays loaded
- [ ] Add a scene stack and history for navigation
- [ ] Add return-to-previous and return-to-menu flows
- [ ] Add a main-menu to gameplay flow
- [ ] Add a data-driven game-flow description
- [ ] Add flow events consumable by gameplay and scripts
- [ ] Add per-build overrides of the startup scene
- [ ] Add safe recovery when the startup scene is missing

## Scene References & Dependencies

- [ ] Add referencing scenes and content by stable identifier
- [ ] Add a dependency graph across scenes and assets
- [ ] Add missing-reference detection and reporting
- [ ] Add reference fixup when a target is renamed or moved
- [ ] Add a fallback for unresolved references
- [ ] Add a dependency browser in the editor
- [ ] Add detection of circular scene dependencies
- [ ] Add validation that all references resolve before save
- [ ] Add preloading of referenced scenes and assets
- [ ] Add reference-usage search across the project

## Scene Bounds, Metadata & Thumbnails

- [ ] Add automatic scene-bounds computation
- [ ] Add editable scene metadata fields
- [ ] Add scene thumbnail capture from a view
- [ ] Add scene statistics (entity count, memory, asset count)
- [ ] Add tags and categories for organizing scenes
- [ ] Add a scene browser showing thumbnails and metadata
- [ ] Add last-edited and authorship tracking

## Level Bake & Preprocessing

- [ ] Add association of baked lighting with a scene
- [ ] Add baked navigation data per scene
- [ ] Add baked occlusion and visibility data per scene
- [ ] Add baked reflection and light probes per scene
- [ ] Add a cook step that prepares a scene for shipping
- [ ] Add incremental rebake of only changed regions
- [ ] Add bake status and dirty tracking
- [ ] Add validation that baked data matches current content
- [ ] Add background baking that does not block editing
- [ ] Add bake artifacts stored alongside the scene

## Scene Validation

- [ ] Add pre-save scene validation
- [ ] Add detection of missing references
- [ ] Add detection of duplicate identifiers
- [ ] Add detection of orphaned and unreachable entities
- [ ] Add detection of content outside scene bounds
- [ ] Add checks for required components and settings
- [ ] Add one-click fixups for common problems
- [ ] Add a validation report panel
- [ ] Add customizable validation rules
- [ ] Add validation as a step in the build pipeline

## Scene Diff, Merge & Version Control

- [ ] Add a granular per-object file layout for scenes
- [ ] Add a text format that produces readable diffs
- [ ] Add diffing of two scene versions
- [ ] Add three-way merge of scene changes
- [ ] Add conflict detection and resolution
- [ ] Add visualization of scene changes in the editor
- [ ] Add version-control status per scene and per object
- [ ] Add locking of scenes or objects during team edits
- [ ] Add merge-friendly stable ordering of serialized content

## Persistence & Runtime State

- [ ] Add persistence of runtime changes to a scene
- [ ] Add save and restore of the active loaded-scene set
- [ ] Add scene state captured in save games
- [ ] Add per-scene reset to authored state
- [ ] Add a distinction between authored content and runtime changes
- [ ] Add versioned migration of persisted scene state

## Editor UX

- [ ] Add a guided new-scene wizard with templates
- [ ] Add scene templates and create-from-template
- [ ] Add a default scene for new projects
- [ ] Add autosave and crash-safe recovery of open scenes
- [ ] Add clear status of loaded, active, and dirty scenes
- [ ] Add plain-language prompts for save and discard
- [ ] Add drag-and-drop of scenes into the world
- [ ] Add a beginner-friendly single-scene mode
- [ ] Add a gallery of example scenes to open and learn from

## Performance & Large Scenes

- [ ] Add async loading and unloading that never stalls the editor
- [ ] Add load-time budgets and profiling per scene
- [ ] Add memory reporting per loaded scene
- [ ] Add handoff to world streaming for very large scenes
- [ ] Add progressive instantiation to smooth load spikes
- [ ] Add background prewarming of likely-next scenes
- [ ] Add diagnostics for slow-loading scenes

## Testing & Validation

- [ ] Add load and unload lifecycle tests
- [ ] Add additive multi-scene tests
- [ ] Add cross-scene reference resolution tests
- [ ] Add transition sequencing tests
- [ ] Add reference-fixup tests for rename and move
- [ ] Add binary and text round-trip serialization tests
- [ ] Add play-mode enter and restore fidelity tests
- [ ] Add scene-migration tests across versions
- [ ] Add save and restore fidelity tests for scene state
- [ ] Add large-scene load-performance stress tests
- [ ] Add validation-rule regression tests

# 12 · Procedural Generation / PCG

## PCG Graph Framework

- [ ] Add a node-based procedural generation graph
- [ ] Add typed pins for points, attributes, meshes, splines, and volumes
- [ ] Add a node registry with categories and metadata
- [ ] Add graph compilation into an executable plan
- [ ] Add a dependency-driven evaluation order
- [ ] Add lazy evaluation that runs only what an output needs
- [ ] Add caching of intermediate node results
- [ ] Add incremental re-evaluation when a single node or parameter changes
- [ ] Add dirty propagation through downstream nodes
- [ ] Add subgraphs and collapsed reusable node groups
- [ ] Add user-defined graph functions with inputs and outputs
- [ ] Add exposed parameters promoted to the graph interface
- [ ] Add loop and iteration constructs with guards
- [ ] Add branch and switch nodes for conditional generation
- [ ] Add multiple named outputs per graph
- [ ] Add graph-level seed and deterministic evaluation
- [ ] Add per-node timing and cost accounting
- [ ] Add graph versioning and migration
- [ ] Add error and warning propagation with node attribution
- [ ] Add cancellation of long-running graph evaluation
- [ ] Add partial evaluation limited to a spatial region
- [ ] Add a graph interpreter and an optional compiled fast path
- [ ] Add graph serialization in binary and diff-friendly text
- [ ] Add hot-reload of graphs edited on disk

## PCG Data Model

- [ ] Add a point-cloud data type with position and orientation
- [ ] Add typed attribute sets attached to points
- [ ] Add scalar, vector, color, bool, integer, and string attributes
- [ ] Add per-point transform (position, rotation, scale)
- [ ] Add density and weight as first-class attributes
- [ ] Add a spatial-data type carrying bounds and sampling
- [ ] Add surface data sampled from meshes and terrain
- [ ] Add volume data for 3D fields and voxels
- [ ] Add spline data with points, tangents, and width
- [ ] Add attribute metadata (range, default, interpolation)
- [ ] Add attribute creation, copy, and removal nodes
- [ ] Add attribute interpolation and blending
- [ ] Add tags and classification attributes on points
- [ ] Add a stable per-point identifier for cross-graph references
- [ ] Add a compact columnar attribute layout for performance
- [ ] Add streaming-friendly chunked point storage
- [ ] Add conversion between points, meshes, splines, and volumes
- [ ] Add attribute schema validation
- [ ] Add memory accounting for point and attribute data
- [ ] Add debug inspection of any data on any pin

## Sampling & Scattering Nodes

- [ ] Add surface scattering that samples points on meshes and terrain
- [ ] Add volume scattering that fills a 3D region
- [ ] Add spline scattering along and around paths
- [ ] Add grid and jittered-grid sampling
- [ ] Add Poisson-disk sampling for even spacing
- [ ] Add blue-noise sampling for natural distribution
- [ ] Add density-driven sample counts from a map or field
- [ ] Add clustered scattering for clumps and groves
- [ ] Add stratified sampling for controlled coverage
- [ ] Add importance sampling weighted by an attribute
- [ ] Add relaxation to even out an existing point set
- [ ] Add minimum-distance enforcement between points
- [ ] Add per-sample random seed derived deterministically
- [ ] Add surface-normal and slope capture at each sample
- [ ] Add barycentric and UV capture at each sample
- [ ] Add boundary and edge sampling
- [ ] Add sampling limited by a mask or selection
- [ ] Add sample-count budgets and caps
- [ ] Add adaptive sampling that refines where needed
- [ ] Add resampling and thinning of dense point sets
- [ ] Add scatter debug visualization
- [ ] Add deterministic scattering stable across runs

## Filtering & Selection Nodes

- [ ] Add filtering by attribute threshold and range
- [ ] Add filtering by slope, height, and curvature
- [ ] Add filtering by proximity to other points or objects
- [ ] Add filtering by a mask or painted region
- [ ] Add filtering by inside/outside a volume or spline
- [ ] Add random selection by percentage and count
- [ ] Add selection by density comparison
- [ ] Add selection by tag and classification
- [ ] Add top-N and bottom-N selection by an attribute
- [ ] Add set operations (union, intersection, difference) on point sets
- [ ] Add spatial partitioning of a set into groups
- [ ] Add expression-based selection with a small formula language
- [ ] Add invert, expand, and contract of a selection
- [ ] Add stable ordering of filtered results
- [ ] Add selection debug visualization

## Transform & Geometry Nodes

- [ ] Add translate, rotate, and scale nodes on point transforms
- [ ] Add align-to-normal and align-to-direction nodes
- [ ] Add random rotation, scale, and jitter within ranges
- [ ] Add snap-to-surface and project-onto-surface nodes
- [ ] Add snap-to-grid and snap-to-spline nodes
- [ ] Add offset along normal and along axis nodes
- [ ] Add look-at and orient-between-points nodes
- [ ] Add curve-following orientation along splines
- [ ] Add bounds and pivot manipulation
- [ ] Add copy-to-points that instances geometry at each point
- [ ] Add mirror, array, and radial-array nodes
- [ ] Add relax and smooth of point positions
- [ ] Add noise-based displacement of transforms
- [ ] Add attribute-driven transform (scale by density, rotate by slope)
- [ ] Add merge, append, and split of point sets
- [ ] Add reorder and sort by an attribute
- [ ] Add transform debug visualization

## Attribute & Math Nodes

- [ ] Add arithmetic nodes over attributes
- [ ] Add vector and matrix math nodes
- [ ] Add remap, clamp, and curve nodes
- [ ] Add random-value nodes seeded deterministically
- [ ] Add gradient and ramp evaluation nodes
- [ ] Add comparison and logic nodes
- [ ] Add attribute copy, rename, and delete nodes
- [ ] Add attribute-from-position and from-normal nodes
- [ ] Add attribute blending and interpolation nodes
- [ ] Add accumulate and reduce nodes over a set
- [ ] Add per-neighbor aggregation nodes
- [ ] Add a compact expression node with a formula language
- [ ] Add color and gradient sampling nodes
- [ ] Add noise-to-attribute nodes
- [ ] Add conditional attribute assignment
- [ ] Add attribute normalization and statistics

## Spatial Query & Proximity Nodes

- [ ] Add nearest-neighbor queries between point sets
- [ ] Add radius and k-nearest neighbor queries
- [ ] Add distance-to-surface and distance-to-spline nodes
- [ ] Add ray and line queries against surfaces
- [ ] Add overlap and containment tests against volumes
- [ ] Add density estimation from local neighborhoods
- [ ] Add clustering by proximity
- [ ] Add graph and connectivity building between points
- [ ] Add shortest-path and network queries over point graphs
- [ ] Add spatial acceleration reuse across queries
- [ ] Add occupancy and collision checks for placement
- [ ] Add proximity debug visualization

## Noise & Field Nodes

- [ ] Add value noise
- [ ] Add gradient (Perlin) noise
- [ ] Add simplex noise
- [ ] Add cellular (Worley) noise with multiple distance metrics
- [ ] Add fractal Brownian motion layering
- [ ] Add ridged and billowed noise variants
- [ ] Add domain warping of noise inputs
- [ ] Add Voronoi cells and edges
- [ ] Add Delaunay triangulation of point sets
- [ ] Add curl and flow noise for directional fields
- [ ] Add gradient and vector fields
- [ ] Add distance fields from points, splines, and surfaces
- [ ] Add tiling and seamless noise options
- [ ] Add analytic derivatives for slope and normals
- [ ] Add noise combination (add, multiply, blend, warp)
- [ ] Add frequency, octave, lacunarity, and gain controls
- [ ] Add masks and falloff shapes as fields
- [ ] Add a field-to-attribute sampling node
- [ ] Add GPU evaluation of noise and fields
- [ ] Add deterministic, seed-driven noise
- [ ] Add field debug visualization

## Spawning & Output Nodes

- [ ] Add mesh spawning at points
- [ ] Add instanced-mesh output for dense placement
- [ ] Add entity spawning into the world from points
- [ ] Add prefab and chunk instancing from points
- [ ] Add material and variation assignment on spawn
- [ ] Add per-instance attribute passthrough (color, wind, scale)
- [ ] Add spline and mesh generation as output
- [ ] Add terrain height and weight output
- [ ] Add foliage-layer output handed to the foliage system
- [ ] Add collision and physics setup on spawned content
- [ ] Add navigation flags on spawned content
- [ ] Add LOD assignment on spawned content
- [ ] Add tagging and grouping of spawned content
- [ ] Add cleanup and regeneration that removes prior output
- [ ] Add bulk spawn through the deferred command buffer
- [ ] Add streaming-aware output tied to cells
- [ ] Add output budgets and caps
- [ ] Add output debug visualization

## Density, Masks & Weight Maps

- [ ] Add painted density and weight masks as inputs
- [ ] Add mask generation from slope, height, and curvature
- [ ] Add mask generation from distance fields
- [ ] Add mask combination (add, multiply, min, max, subtract)
- [ ] Add mask blur, sharpen, and threshold
- [ ] Add mask remap and curve shaping
- [ ] Add mask from image and imported data
- [ ] Add mask from selection and tags
- [ ] Add mask preview and visualization
- [ ] Add per-layer mask stacks
- [ ] Add mask-driven density in sampling nodes
- [ ] Add mask baking to a reusable asset
- [ ] Add mask resolution and memory controls

## Rule & Constraint Solving

- [ ] Add a tile set with adjacency rules
- [ ] Add constraint-based tile solving over a grid
- [ ] Add wave-function-collapse-style propagation
- [ ] Add backtracking and contradiction recovery
- [ ] Add weighted tile probabilities
- [ ] Add edge and corner socket matching
- [ ] Add 3D tile solving for volumes
- [ ] Add pre-placed constraints and seeds
- [ ] Add border and boundary constraints
- [ ] Add symmetry and rotation of tiles
- [ ] Add hierarchical constraint solving for large grids
- [ ] Add region-limited and incremental solving
- [ ] Add deterministic solving from a seed
- [ ] Add solver timeout and fallback handling
- [ ] Add authoring of tile sets and adjacency
- [ ] Add solver debug visualization of collapse steps
- [ ] Add validation that solutions satisfy all constraints

## Grammar & L-System Generation

- [ ] Add a rule-based grammar system
- [ ] Add an L-system interpreter for branching structures
- [ ] Add stochastic and context-sensitive rules
- [ ] Add parameterized grammar symbols
- [ ] Add turtle-style geometry interpretation
- [ ] Add shape-grammar subdivision of volumes
- [ ] Add facade and floor-plan grammars
- [ ] Add plant and tree grammars
- [ ] Add rule weighting and randomization
- [ ] Add recursion depth limits and guards
- [ ] Add grammar authoring and editing tools
- [ ] Add deterministic grammar expansion from a seed
- [ ] Add grammar-to-mesh and grammar-to-points output
- [ ] Add grammar debug visualization of derivation

## Voxel & Volumetric Generation

- [ ] Add a voxel density field data type
- [ ] Add signed-distance-field authoring and combination
- [ ] Add boolean operations on volumes (union, subtract, intersect)
- [ ] Add smooth and sharp blending of volumes
- [ ] Add marching-cubes surface extraction
- [ ] Add dual-contouring for sharp features
- [ ] Add adaptive resolution for volume meshing
- [ ] Add material assignment per voxel region
- [ ] Add noise and field-driven volume carving
- [ ] Add cave and tunnel carving operators
- [ ] Add overhang and arch generation
- [ ] Add volume-to-collision generation
- [ ] Add chunked voxel volumes for streaming
- [ ] Add GPU-assisted volume meshing
- [ ] Add seam-free meshing across chunk boundaries
- [ ] Add volume debug visualization

## Procedural Mesh Generation

- [ ] Add a procedural mesh builder with vertices and indices
- [ ] Add primitive generators (plane, box, sphere, cylinder, cone)
- [ ] Add extrude, bevel, and inset operations
- [ ] Add loft and sweep along splines
- [ ] Add revolve and lathe operations
- [ ] Add boolean mesh operations
- [ ] Add subdivision and smoothing
- [ ] Add automatic UV generation and unwrapping
- [ ] Add automatic normal and tangent generation
- [ ] Add vertex-color and attribute painting on generated meshes
- [ ] Add mesh triangulation of point sets and outlines
- [ ] Add mesh simplification of generated geometry
- [ ] Add automatic LOD generation for procedural meshes
- [ ] Add collision generation for procedural meshes
- [ ] Add mesh caching and reuse
- [ ] Add procedural-mesh output to the spawn system
- [ ] Add mesh validation (manifold, winding, degenerate checks)

## Terrain Generation

- [ ] Add heightfield generation from noise and fields
- [ ] Add layered terrain generation with combined octaves
- [ ] Add ridge, mountain, and valley operators
- [ ] Add hydraulic and thermal erosion nodes
- [ ] Add river and lake carving from flow
- [ ] Add slope, height, and curvature masks for materials
- [ ] Add automatic material-weight generation
- [ ] Add automatic scatter placement on generated terrain
- [ ] Add terrain-hole and cave-entrance generation
- [ ] Add region-limited terrain generation inside a brush
- [ ] Add blending of generated terrain with hand edits
- [ ] Add seamless generation across terrain tiles
- [ ] Add real-world data as a generation input
- [ ] Add deterministic terrain generation from a seed
- [ ] Add output directly into the terrain system
- [ ] Add terrain-generation presets (islands, mountains, deserts)
- [ ] Add live preview of terrain generation
- [ ] Add non-destructive terrain-generation layers

## Scatter & Ecosystem Generation

- [ ] Add ecosystem rules combining species, density, and constraints
- [ ] Add competition and exclusion between species
- [ ] Add layered vegetation (canopy, understory, ground cover)
- [ ] Add moisture, temperature, and light inputs
- [ ] Add slope, height, and soil constraints
- [ ] Add clustering and grouping into natural stands
- [ ] Add age and size variation across placements
- [ ] Add avoidance around roads, water, and structures
- [ ] Add succession and growth simulation over time
- [ ] Add seed-dispersal-style spreading
- [ ] Add density and weight maps for control
- [ ] Add deterministic ecosystem generation from a seed
- [ ] Add output directly into the foliage system
- [ ] Add ecosystem presets (forest, meadow, tundra, jungle)
- [ ] Add live repopulation when the surface changes
- [ ] Add ecosystem debug visualization
- [ ] Add region-limited ecosystem painting

## Road, Path & Network Generation

- [ ] Add road-network generation from a graph
- [ ] Add cost-based path routing over terrain
- [ ] Add slope and obstacle avoidance in routing
- [ ] Add intersection and junction generation
- [ ] Add hierarchical networks (highways, streets, alleys)
- [ ] Add path smoothing and banking
- [ ] Add bridge and tunnel generation where paths cross obstacles
- [ ] Add terrain conforming and carving along paths
- [ ] Add roadside prop and detail placement
- [ ] Add river-network generation from flow accumulation
- [ ] Add trail and footpath generation between points of interest
- [ ] Add spline output for downstream tools
- [ ] Add deterministic network generation from a seed
- [ ] Add connectivity validation of the network
- [ ] Add network debug visualization
- [ ] Add network presets (grid city, organic town, rural)

## Building & Structure Generation

- [ ] Add modular building assembly from a kit of parts
- [ ] Add floor-plan generation with rooms and connections
- [ ] Add facade generation with windows, doors, and trim
- [ ] Add multi-floor stacking with consistent alignment
- [ ] Add roof generation (flat, gabled, hipped, complex)
- [ ] Add stair and connection generation between floors
- [ ] Add interior wall and partition generation
- [ ] Add furniture and prop placement inside rooms
- [ ] Add structural constraint checks (support, openings)
- [ ] Add material and style themes for buildings
- [ ] Add level-of-detail proxies for distant buildings
- [ ] Add damage, age, and wear variation
- [ ] Add collision and navigation for generated buildings
- [ ] Add snapping of buildings to terrain and plots
- [ ] Add deterministic building generation from a seed
- [ ] Add building presets and style libraries
- [ ] Add building-generation debug visualization
- [ ] Add non-destructive editing of generated buildings

## City & Urban Layout Generation

- [ ] Add block and parcel subdivision from a road network
- [ ] Add lot allocation and building placement per parcel
- [ ] Add zoning (residential, commercial, industrial, parks)
- [ ] Add population-density-driven building height and density
- [ ] Add landmark and point-of-interest placement
- [ ] Add street furniture, lighting, and signage placement
- [ ] Add sidewalk, plaza, and open-space generation
- [ ] Add district and neighborhood theming
- [ ] Add terrain-aware city layout on uneven ground
- [ ] Add river and coastline integration into the layout
- [ ] Add traffic and pedestrian route hints
- [ ] Add deterministic city generation from a seed
- [ ] Add city presets (grid, medieval, modern, coastal)
- [ ] Add city-generation debug visualization
- [ ] Add region-limited and incremental city generation

## Interior & Dungeon Generation

- [ ] Add room-graph generation with connections
- [ ] Add grid, cellular, and organic layout algorithms
- [ ] Add corridor and hallway carving between rooms
- [ ] Add door, lock, and key placement
- [ ] Add room templates and hand-authored set-pieces
- [ ] Add encounter, loot, and spawn placement
- [ ] Add difficulty and pacing curves across the layout
- [ ] Add guaranteed connectivity and reachability
- [ ] Add critical-path and optional-branch generation
- [ ] Add theming and biome variation per region
- [ ] Add trap and hazard placement
- [ ] Add prop and decoration scattering
- [ ] Add multi-floor and vertical dungeon generation
- [ ] Add mesh assembly from the layout
- [ ] Add collision and navigation for generated interiors
- [ ] Add deterministic dungeon generation from a seed
- [ ] Add validation that every area is reachable
- [ ] Add dungeon-generation debug visualization

## Cave & Cavern Generation

- [ ] Add cave generation from volumetric fields
- [ ] Add tunnel carving between chambers
- [ ] Add chamber and cavern shaping operators
- [ ] Add stalactite, stalagmite, and formation placement
- [ ] Add water and pool placement in caves
- [ ] Add entrance and exit generation connecting to the surface
- [ ] Add mesh extraction and material assignment
- [ ] Add collision and navigation for caves
- [ ] Add chunked cave generation for streaming
- [ ] Add deterministic cave generation from a seed
- [ ] Add cave-generation debug visualization

## Biome & World Generation

- [ ] Add a world-scale biome map from climate inputs
- [ ] Add temperature, moisture, and elevation models
- [ ] Add biome assignment and blending zones
- [ ] Add continent, ocean, and coastline generation
- [ ] Add mountain-range and plate-style features
- [ ] Add river-network generation across the world
- [ ] Add biome-specific terrain, scatter, and material rules
- [ ] Add points-of-interest and settlement placement
- [ ] Add world-map preview and overview
- [ ] Add seed-driven whole-world generation
- [ ] Add region extraction for detailed generation
- [ ] Add layering of hand-authored regions over generated ones
- [ ] Add streaming-aware world generation
- [ ] Add world-generation presets (archipelago, continent, wasteland)
- [ ] Add world-generation debug visualization
- [ ] Add validation of biome coverage and transitions

## Procedural Materials & Texturing

- [ ] Add procedural texture generation from noise and fields
- [ ] Add layered material blending driven by generated masks
- [ ] Add weathering and aging effects from curvature and cavity
- [ ] Add per-object color and pattern variation
- [ ] Add procedural decal and detail placement
- [ ] Add tiling and macro-variation control
- [ ] Add baking of procedural textures to assets
- [ ] Add handoff to the material system
- [ ] Add deterministic texture generation from a seed
- [ ] Add live preview of procedural materials
- [ ] Add procedural-material presets
- [ ] Add resolution and memory controls

## Runtime & Endless Generation

- [ ] Add runtime evaluation of graphs during play
- [ ] Add generation triggered by streaming cell load
- [ ] Add endless and infinite-world generation around the camera
- [ ] Add chunked generation aligned to world cells
- [ ] Add seamless generation across chunk boundaries
- [ ] Add async generation off the main thread
- [ ] Add background prefetch of soon-visible generation
- [ ] Add deterministic generation so a seed reproduces the world
- [ ] Add regeneration when parameters or inputs change
- [ ] Add partial regeneration of only affected regions
- [ ] Add generation budgets to avoid frame hitches
- [ ] Add caching of generated chunks to disk
- [ ] Add eviction of distant generated chunks
- [ ] Add persistence of runtime edits over generated content
- [ ] Add reconciliation of hand edits with regenerated content
- [ ] Add gameplay-driven generation (spawn a structure on demand)
- [ ] Add streaming handoff of generated content to the world system
- [ ] Add generation progress reporting
- [ ] Add cancellation of in-flight generation
- [ ] Add runtime generation diagnostics

## Determinism & Seeding

- [ ] Add a global generation seed
- [ ] Add per-graph and per-node seed derivation
- [ ] Add spatially stable seeding so a location always generates the same
- [ ] Add counter-based reproducible random streams
- [ ] Add order-independent seeding for parallel evaluation
- [ ] Add seed exposure in parameters and presets
- [ ] Add reproducibility across platforms and hardware
- [ ] Add seed-variation tools to explore alternatives
- [ ] Add locking of seeds for finalized content
- [ ] Add determinism validation across runs
- [ ] Add determinism validation across thread counts

## ECS Integration & Bulk Generation

- [ ] Spawn generated content as archetype/chunk data
- [ ] Add bulk entity creation through the deferred command buffer
- [ ] Add parallel graph evaluation across the worker pool
- [ ] Add SIMD-friendly point and attribute layouts
- [ ] Add job-graph scheduling of generation stages
- [ ] Add chunked evaluation matching ECS chunk sizes
- [ ] Add zero-copy handoff of generated instances to GPU buffers
- [ ] Add memory-traffic-aware batch sizes for generation
- [ ] Add deterministic parallel generation stable across threads
- [ ] Add streaming of generated ECS chunks in and out
- [ ] Add bulk despawn and recycling of generated entities
- [ ] Add scaling to millions of generated instances within budget
- [ ] Add generation that reuses foliage and instancing paths
- [ ] Add cache-friendly output layouts for downstream systems
- [ ] Add throughput diagnostics for bulk generation
- [ ] Add a stress harness for extreme generated counts

## Streaming & World Integration

- [ ] Add per-cell generation tied to world streaming
- [ ] Add deterministic per-cell seeds from cell coordinates
- [ ] Add seam matching so adjacent cells align
- [ ] Add generation ahead of the streaming radius
- [ ] Add eviction of generated content with cell unload
- [ ] Add persistence of edits to generated cells
- [ ] Add origin-rebasing awareness for generated content
- [ ] Add priority generation around streaming sources
- [ ] Add proxy generation for distant regions
- [ ] Add handoff of generated collision and navigation
- [ ] Add generated-content residency budgets
- [ ] Add integration with terrain, foliage, and water systems
- [ ] Add streaming-generation diagnostics

## Non-Destructive Procedural Stack

- [ ] Add a procedural layer stack over authored content
- [ ] Add per-layer graphs applied in order
- [ ] Add masks limiting where each layer applies
- [ ] Add blend modes between procedural and authored content
- [ ] Add reorder, toggle, and solo of procedural layers
- [ ] Add preservation of hand edits under regeneration
- [ ] Add bake of the stack into final content on demand
- [ ] Add per-layer parameters and presets
- [ ] Add live recompute with cached intermediate results
- [ ] Add copy and reuse of procedural layers across content
- [ ] Add a stack-cost readout

## Graph Authoring & Editor

- [ ] Add a node-graph editor canvas
- [ ] Add a searchable node palette with categories
- [ ] Add drag-to-connect typed pins with validation
- [ ] Add live preview of graph output in the viewport
- [ ] Add per-node preview of intermediate data
- [ ] Add exposed-parameter panels with plain-language labels
- [ ] Add subgraph creation, collapse, and expand
- [ ] Add comments, groups, and reroute nodes
- [ ] Add copy, paste, and duplicate of node selections
- [ ] Add undo and redo across graph edits
- [ ] Add a node reference and inline documentation
- [ ] Add graph templates and starting points
- [ ] Add a graph library and reusable modules
- [ ] Add error and warning highlighting on nodes
- [ ] Add pin value inspection and pinning
- [ ] Add on-canvas parameter tweaking with live update
- [ ] Add layout auto-arrange and alignment helpers
- [ ] Add search and navigation within large graphs
- [ ] Add versioned graph assets with migration
- [ ] Add a gallery of example graphs to open and learn from
- [ ] Add a beginner mode with simplified nodes
- [ ] Add graph diff and merge for source control

## Debugging & Data Inspection

- [ ] Add visualization of points, densities, and attributes
- [ ] Add attribute heatmaps and color mapping
- [ ] Add per-node data statistics (count, ranges, memory)
- [ ] Add step-through evaluation of the graph
- [ ] Add isolation of a single node's output
- [ ] Add timing and cost profiling per node
- [ ] Add highlighting of the most expensive nodes
- [ ] Add inspection of seeds and random draws
- [ ] Add comparison of two graph results
- [ ] Add capture and replay of a generation run
- [ ] Add warnings for empty or degenerate outputs
- [ ] Add validation of attribute schemas across the graph
- [ ] Add a data inspector for any pin

## One-Click Generators & Templates

- [ ] Add one-click "generate a forest" over a region
- [ ] Add one-click "generate a city" on the terrain
- [ ] Add one-click "generate a dungeon"
- [ ] Add one-click "generate caves" under the terrain
- [ ] Add one-click "generate a mountain range"
- [ ] Add one-click "generate a river system"
- [ ] Add one-click "scatter rocks and debris"
- [ ] Add one-click "populate this area" from environment rules
- [ ] Add plain-language parameters (more trees, denser, wilder)
- [ ] Add sensible defaults that produce good results immediately
- [ ] Add draw-a-region-then-generate workflow
- [ ] Add brush-based procedural painting of rules
- [ ] Add live preview before committing a generator
- [ ] Add always-available undo of generated content
- [ ] Add a generator gallery with thumbnails
- [ ] Add guided wizards for complex generators
- [ ] Add a beginner mode that hides graph internals
- [ ] Add one-click regenerate with a new seed

## Presets, Parameters & Reusable Modules

- [ ] Add named presets bundling a graph and its parameters
- [ ] Add a preset library with categories and thumbnails
- [ ] Add exposed parameters with ranges and defaults
- [ ] Add parameter randomization within safe ranges
- [ ] Add reusable module graphs shared across projects
- [ ] Add capture of a generated result into a preset
- [ ] Add layering of presets and overrides
- [ ] Add import and export of presets and modules
- [ ] Add versioning of presets and modules
- [ ] Add parameter validation and dependency rules
- [ ] Add a favorites and recents list
- [ ] Add community-shareable preset packaging

## Performance, Budgets & Scaling

- [ ] Add per-graph time and memory budgets
- [ ] Add async and incremental generation to avoid stalls
- [ ] Add parallel evaluation of independent branches
- [ ] Add GPU acceleration of heavy nodes (noise, fields, scatter)
- [ ] Add caching and reuse of expensive intermediate results
- [ ] Add spatial chunking to bound working-set size
- [ ] Add level-of-detail generation for distant content
- [ ] Add quality presets scaling generation detail
- [ ] Add adaptive throttling from performance headroom
- [ ] Add memory budgets and eviction for generated data
- [ ] Add profiling and cost attribution per node and graph
- [ ] Add a headless generation benchmark harness
- [ ] Add machine-readable generation metrics for CI
- [ ] Add over-budget diagnostics with responsible nodes

## Testing & Validation

- [ ] Add determinism tests for a fixed seed
- [ ] Add cross-thread and cross-platform reproducibility tests
- [ ] Add node unit tests for each node type
- [ ] Add graph golden-output tests
- [ ] Add constraint-solver correctness tests
- [ ] Add reachability and connectivity tests for dungeons and cities
- [ ] Add seam-continuity tests across generated chunks
- [ ] Add attribute-schema validation tests
- [ ] Add regeneration-stability tests after edits
- [ ] Add streaming-generation integration tests
- [ ] Add large-scale generation performance stress tests
- [ ] Add memory-budget and eviction tests
- [ ] Add degenerate-output detection tests
- [ ] Add golden-image tests for representative generated scenes

# 13 · Animation System

## Skeleton & Rig

- [ ] Add a skeleton asset with a bone hierarchy
- [ ] Add per-bone reference (bind) pose transforms
- [ ] Add bone parent indices and traversal order
- [ ] Add inverse bind matrices for skinning
- [ ] Add named bones with stable identifiers
- [ ] Add bone groups and chains for tools
- [ ] Add sockets and attachment points on bones
- [ ] Add attach and detach of entities to sockets
- [ ] Add skeleton compatibility and remap metadata
- [ ] Add a standardized rig-mapping layer for cross-skeleton sharing
- [ ] Add bone display and gizmo metadata for the editor
- [ ] Add virtual and helper bones for tools
- [ ] Add per-bone constraints metadata
- [ ] Add skeleton validation (loops, missing parents, scale)
- [ ] Add skeleton import from asset formats
- [ ] Add skeleton versioning and migration

## Skinning & Deformation

- [ ] Add linear-blend skinning
- [ ] Add dual-quaternion skinning to reduce candy-wrapper artifacts
- [ ] Add optimized-center-of-rotation skinning for volume preservation
- [ ] Add per-vertex bone indices and weights
- [ ] Add a configurable maximum influences per vertex
- [ ] Add GPU skinning with a bone-matrix palette
- [ ] Add a CPU skinning fallback path
- [ ] Add a skin cache reused across passes
- [ ] Add skinning for depth, shadow, and velocity passes
- [ ] Add previous-frame skinned positions for motion vectors
- [ ] Add normal and tangent skinning
- [ ] Add non-uniform-scale handling
- [ ] Add delta-mush smoothing deformer
- [ ] Add tension and stretch-driven deformation
- [ ] Add corrective and pose-space deformers
- [ ] Add a skin-wrap deformer for proxy-driven meshes
- [ ] Add lattice and cage deformers
- [ ] Add blend-shape-plus-skin combined deformation
- [ ] Add deformer stacking with configurable order
- [ ] Add skinning and deformation validation and cost diagnostics

## Animation Clips & Data

- [ ] Add an animation clip asset with per-bone tracks
- [ ] Add position, rotation, and scale keyframes per bone
- [ ] Add scalar curve tracks for custom values
- [ ] Add configurable interpolation (linear, cubic, stepped)
- [ ] Add clip sampling at arbitrary time
- [ ] Add looping and clamping modes
- [ ] Add clip duration, frame rate, and playback range
- [ ] Add additive clips relative to a reference pose
- [ ] Add clip metadata (length, bone set, root motion presence)
- [ ] Add clip trimming, cropping, and time-warping
- [ ] Add clip concatenation and stitching
- [ ] Add per-clip event and marker tracks
- [ ] Add an animation library grouping many clips
- [ ] Add streaming of clip data on demand
- [ ] Add clip import from asset formats
- [ ] Add clip validation against a skeleton
- [ ] Add clip versioning and migration
- [ ] Add deterministic sampling for tests and networking

## Pose & Evaluation Core

- [ ] Add a pose representation in local bone space
- [ ] Add conversion between local and model space
- [ ] Add a pose blend primitive (linear interpolation)
- [ ] Add per-bone weighted blending
- [ ] Add additive pose application
- [ ] Add a reference-pose and identity-pose source
- [ ] Add a pose stack for layered evaluation
- [ ] Add an evaluation graph of pose-producing nodes
- [ ] Add lazy evaluation of only-needed bones
- [ ] Add a pose cache to reuse results within a frame
- [ ] Add thread-safe pose evaluation off the main thread
- [ ] Add scratch-buffer pooling for evaluation
- [ ] Add pose normalization and quaternion continuity
- [ ] Add bone-mask-aware evaluation
- [ ] Add evaluation ordering and dependency resolution
- [ ] Add pose-evaluation cost accounting

## Rigging Tools & Skeleton Authoring

- [ ] Add creation of skeletons and bones in the editor
- [ ] Add bone insert, delete, split, and merge
- [ ] Add bone rename, reparent, and reorder
- [ ] Add interactive bone placement in the viewport
- [ ] Add bone orientation and roll adjustment
- [ ] Add automatic bone-axis orientation
- [ ] Add chain creation for spines, limbs, and tails
- [ ] Add symmetry so edits mirror across an axis
- [ ] Add snapping of bones to mesh features
- [ ] Add joint gizmos and manipulators
- [ ] Add bone length, radius, and display shape controls
- [ ] Add markers for sockets and attachment points
- [ ] Add bone color, group, and layer organization
- [ ] Add a skeleton templates library (biped, quadruped, bird, custom)
- [ ] Add reference-mesh alignment guides
- [ ] Add validation and cleanup of the authored skeleton
- [ ] Add undo and redo across skeleton edits
- [ ] Add export of authored skeletons

## Skin Weighting & Deformation Authoring

- [ ] Add automatic weight binding on skin attach
- [ ] Add heat-map and geodesic-distance auto-weighting
- [ ] Add a weight-painting brush with add, subtract, and smooth
- [ ] Add per-bone weight visualization
- [ ] Add weight normalization and max-influence limiting
- [ ] Add weight mirroring across a symmetry axis
- [ ] Add weight smoothing, sharpening, and flooding
- [ ] Add weight copy and transfer between meshes
- [ ] Add weight pruning of tiny influences
- [ ] Add locking of specific bone weights while painting
- [ ] Add envelope and falloff-based weighting
- [ ] Add component and vertex selection for targeted editing
- [ ] Add weight editing by numeric entry and tables
- [ ] Add gradient and along-bone weighting tools
- [ ] Add a deformation preview while posing
- [ ] Add detection and fixing of unweighted vertices
- [ ] Add undo and redo across weight edits
- [ ] Add weighting validation and reports

## Auto-Rigging

- [ ] Add one-click rig generation for standard characters
- [ ] Add automatic joint placement from a mesh
- [ ] Add rig templates for biped and quadruped
- [ ] Add guided marker placement for auto-rig
- [ ] Add automatic control-rig generation on top of the skeleton
- [ ] Add automatic skin binding after auto-rig
- [ ] Add symmetry-aware auto-rigging
- [ ] Add finger, toe, and face auto-rig options
- [ ] Add scale and proportion adaptation to the mesh
- [ ] Add validation and a fix-up pass after auto-rig
- [ ] Add re-run of auto-rig preserving manual tweaks
- [ ] Add auto-rig presets and a beginner one-click path

## Animator Rig Controls

- [ ] Add authored control shapes bound to bones
- [ ] Add a control hierarchy separate from the deformation skeleton
- [ ] Add IK and FK controls with switching
- [ ] Add IK/FK matching to preserve pose on switch
- [ ] Add space switching for controls (world, parent, custom)
- [ ] Add pole-vector and aim controls
- [ ] Add custom control colors, shapes, and sizes
- [ ] Add a control picker UI for fast selection
- [ ] Add selection sets and control groups
- [ ] Add forward and backward rig solving
- [ ] Add secondary controls for offsets and tweaks
- [ ] Add attribute controls exposed on rig nodes
- [ ] Add a visual rig-graph for control logic
- [ ] Add reusable rig modules (arm, leg, spine, hand)
- [ ] Add rig mirroring and symmetry
- [ ] Add rig evaluation off the main thread
- [ ] Add baking of control-rig animation to bone keys
- [ ] Add importing control animation back onto the rig
- [ ] Add rig validation and cycle detection
- [ ] Add a rig-controls debug and display toggle

## Procedural & Runtime Constraint Rigging

- [ ] Add a runtime rig that applies constraints after animation
- [ ] Add aim, position, rotation, and scale constraints
- [ ] Add parent and multi-parent constraints with weights
- [ ] Add look-at chains for heads, spines, and tails
- [ ] Add spring and jiggle bones for secondary motion
- [ ] Add damped follow constraints
- [ ] Add distance and pole constraints
- [ ] Add driven bones (one bone drives another via curves)
- [ ] Add pose-driver (radial-basis) corrective poses
- [ ] Add corrective blend shapes driven by bone angles
- [ ] Add twist distribution along limbs
- [ ] Add bone-chain physics for cloth-like appendages
- [ ] Add constraint ordering and an evaluation stack
- [ ] Add per-constraint weight and blending
- [ ] Add runtime rig evaluation off the main thread
- [ ] Add rig debug visualization
- [ ] Add rig validation and cycle detection

## Keyframe Animation & Posing

- [ ] Add setting keyframes on bones and controls
- [ ] Add auto-key that records changes while posing
- [ ] Add key on selected, on all, and on modified channels
- [ ] Add a posing mode with interactive manipulators
- [ ] Add copy, paste, and mirror of poses
- [ ] Add a pose library with thumbnails
- [ ] Add applying and blending library poses by percentage
- [ ] Add holding, breakdown, and in-between key tools
- [ ] Add tween and favor tools between keys
- [ ] Add push, exaggerate, and dampen pose tools
- [ ] Add snapping controls to the ground and to targets
- [ ] Add pinning of effectors while posing
- [ ] Add symmetry posing across an axis
- [ ] Add selection sets for fast channel keying
- [ ] Add key deletion, insertion, and moving
- [ ] Add a playback and scrub bar with ranges and loop
- [ ] Add a sticky and editable current-frame value display
- [ ] Add pose reset to reference or to a stored pose
- [ ] Add undo and redo across posing
- [ ] Add deterministic authored output

## Curve & Graph Editor

- [ ] Add a curve editor showing animation channels
- [ ] Add editing of keys with position and value handles
- [ ] Add tangent types (auto, linear, flat, stepped, broken)
- [ ] Add tangent weighting and free handles
- [ ] Add ease-in and ease-out presets
- [ ] Add box and lasso selection of keys
- [ ] Add move, scale, and retime of key selections
- [ ] Add snapping to frames and value grids
- [ ] Add channel filtering and isolation
- [ ] Add curve smoothing, simplify, and resample filters
- [ ] Add a noise and jitter generator on curves
- [ ] Add pre- and post-infinity cycle modes
- [ ] Add copy and paste of curve segments
- [ ] Add a value ladder and numeric key entry
- [ ] Add multi-curve normalized view
- [ ] Add a read-only reference curve overlay
- [ ] Add undo and redo across curve edits
- [ ] Add a beginner-friendly simplified curve mode

## Dope Sheet & Timeline Editing

- [ ] Add a dope sheet showing keys per channel and object
- [ ] Add move, scale, and ripple edits of keys
- [ ] Add box selection and multi-object editing
- [ ] Add snapping, frame stepping, and key navigation
- [ ] Add summary tracks that aggregate child keys
- [ ] Add time-range selection and looping
- [ ] Add scaling of timing to change speed
- [ ] Add insert, delete, and shift of time
- [ ] Add key color-coding by channel type
- [ ] Add a synced current-frame indicator across editors
- [ ] Add marker and annotation tracks on the timeline
- [ ] Add undo and redo across timeline edits

## Non-Linear Animation & Authoring Layers

- [ ] Add non-linear clips arranged on tracks
- [ ] Add trim, slip, and time-scale of clips
- [ ] Add crossfade and blend between clips
- [ ] Add additive and override tracks
- [ ] Add authoring animation layers with weights
- [ ] Add per-layer bone masks
- [ ] Add reorder, solo, and mute of layers and tracks
- [ ] Add merging and flattening of layers to keys
- [ ] Add clip looping and hold on tracks
- [ ] Add transition clips with blend curves
- [ ] Add reuse of a clip in multiple places
- [ ] Add extraction of a sub-range into a new clip
- [ ] Add baking of the non-linear result to a single clip
- [ ] Add non-linear editing preview
- [ ] Add undo and redo across non-linear edits
- [ ] Add validation of track and layer coverage

## Animation Baking & Cleanup

- [ ] Add baking of simulation and constraints to keyframes
- [ ] Add baking of control-rig motion to bone keys
- [ ] Add plotting of a channel to dense keys
- [ ] Add resampling to a target frame rate
- [ ] Add an euler-filter to remove rotation flips
- [ ] Add key reduction with an error tolerance
- [ ] Add smoothing and noise-removal passes
- [ ] Add gap filling and hold cleanup
- [ ] Add root and pivot re-centering
- [ ] Add offset, scale, and time-shift of baked results
- [ ] Add bake ranges and selective channel baking
- [ ] Add non-destructive bake previews
- [ ] Add validation of baked output against the source
- [ ] Add batch baking across many clips

## Onion Skinning & Reference

- [ ] Add onion-skin ghosts of past and future frames
- [ ] Add configurable ghost count, spacing, and color
- [ ] Add per-object onion-skin toggles
- [ ] Add motion trails for selected controls
- [ ] Add editable motion trails that move keys in the viewport
- [ ] Add reference-video overlay in the viewport
- [ ] Add reference-image planes for posing
- [ ] Add a side-by-side reference playback panel
- [ ] Add annotation and grease-pencil sketching over frames
- [ ] Add capture of the current view as a reference

## Animation Graph & State Machines

- [ ] Add an animation state machine
- [ ] Add states that play clips or sub-graphs
- [ ] Add transitions with conditions and priorities
- [ ] Add transition blend durations and curves
- [ ] Add entry, default, and exit states
- [ ] Add any-state transitions
- [ ] Add nested and hierarchical sub-state machines
- [ ] Add transition interruption and re-entry rules
- [ ] Add conduits and shared transition logic
- [ ] Add graph parameters (float, int, bool, trigger, vector)
- [ ] Add parameter-driven conditions and expressions
- [ ] Add state entry, update, and exit callbacks
- [ ] Add automatic and time-based transitions
- [ ] Add transition blend by source and destination pose
- [ ] Add caching of pose results across the graph
- [ ] Add graph functions and reusable sub-graphs
- [ ] Add per-state playback speed and time scaling
- [ ] Add relevancy so inactive branches are skipped
- [ ] Add a data-driven graph asset format
- [ ] Add graph versioning and migration
- [ ] Add graph evaluation off the main thread
- [ ] Add deterministic graph evaluation for tests

## Blend Trees & Blend Spaces

- [ ] Add a 1D blend space driven by one parameter
- [ ] Add a 2D directional blend space for locomotion
- [ ] Add a 2D freeform blend space
- [ ] Add nested blend trees
- [ ] Add per-sample clip references and positions
- [ ] Add weighted N-way blending
- [ ] Add automatic weight computation from parameters
- [ ] Add blend smoothing and parameter damping
- [ ] Add per-sample playback-rate scaling for speed warping
- [ ] Add sync-group alignment across blended clips
- [ ] Add blend-space authoring with sample placement
- [ ] Add blend-space preview and grid visualization
- [ ] Add deterministic blend evaluation
- [ ] Add blend-space validation for coverage gaps

## Layered & Masked Blending

- [ ] Add animation layers evaluated in order
- [ ] Add per-layer weight control
- [ ] Add bone masks limiting a layer to a subset of bones
- [ ] Add override and additive layer modes
- [ ] Add per-bone blend weights within a mask
- [ ] Add smooth blend-in and blend-out of layers
- [ ] Add upper-body and lower-body split examples
- [ ] Add mask authoring with bone selection and falloff
- [ ] Add layer priority and conflict resolution
- [ ] Add masked additive layers for reactions and aiming
- [ ] Add per-layer sync options
- [ ] Add layer debug visualization

## Additive & Difference Animation

- [ ] Add additive-clip creation from a base and target pose
- [ ] Add reference-pose subtraction for difference clips
- [ ] Add additive blending onto a base pose
- [ ] Add additive weight and masking
- [ ] Add aim and lean additive layers
- [ ] Add breathing and idle-variation additives
- [ ] Add hit-reaction additives blended over locomotion
- [ ] Add additive-space validation
- [ ] Add additive preview in the editor

## Inverse Kinematics

- [ ] Add a two-bone IK solver
- [ ] Add a FABRIK chain solver
- [ ] Add a cyclic-coordinate-descent solver
- [ ] Add a look-at (aim) solver
- [ ] Add pole-vector control for elbow and knee direction
- [ ] Add foot placement IK aligned to ground
- [ ] Add ground-normal detection and foot roll
- [ ] Add hip and pelvis adjustment for foot IK
- [ ] Add hand IK for weapon and prop grips
- [ ] Add full-body IK with multiple effectors
- [ ] Add IK goals with position and rotation targets
- [ ] Add per-effector weight and blend
- [ ] Add joint limits and constraints
- [ ] Add stretch and squash limits per chain
- [ ] Add IK/FK blending
- [ ] Add solver iteration and tolerance controls
- [ ] Add stable and deterministic convergence
- [ ] Add IK on top of the animation graph output
- [ ] Add IK target authoring and runtime binding
- [ ] Add IK solver cost budgets
- [ ] Add IK debug visualization of goals and chains
- [ ] Add IK convergence validation

## Retargeting

- [ ] Add a standardized humanoid bone abstraction
- [ ] Add mapping from a skeleton to the abstraction
- [ ] Add retargeting of clips between compatible skeletons
- [ ] Add translation-retention rules per bone
- [ ] Add proportion and scale compensation
- [ ] Add pose-based retarget alignment (T-pose or A-pose)
- [ ] Add per-bone retarget mode (animation, skeleton, animation-scaled)
- [ ] Add root and pelvis retargeting for locomotion
- [ ] Add finger and face retargeting options
- [ ] Add live retargeting at runtime
- [ ] Add retargeting on import with baking
- [ ] Add interactive retarget-pose editing
- [ ] Add a chain and limb mapping editor
- [ ] Add retarget preview and side-by-side comparison
- [ ] Add retarget-profile assets reusable across characters
- [ ] Add batch retargeting of animation sets
- [ ] Add retargeting validation and mismatch reporting
- [ ] Add a mismatch fallback that preserves a usable pose
- [ ] Add retargeting between differing topologies (biped to quadruped hints)
- [ ] Add retargeting determinism for tests

## Root Motion & Motion Extraction

- [ ] Add root-motion extraction from clips
- [ ] Add application of root motion to the owning entity
- [ ] Add in-place playback that discards root motion
- [ ] Add root motion accumulation across a frame
- [ ] Add root motion from blended and layered sources
- [ ] Add root motion from the animation graph
- [ ] Add motion warping to hit precise targets
- [ ] Add curve-driven speed and direction adjustment
- [ ] Add root-motion and physics-controller reconciliation
- [ ] Add turn-in-place and pivot handling
- [ ] Add automatic root-bone detection and authoring
- [ ] Add extraction from a chosen bone or a virtual root
- [ ] Add networked root-motion synchronization
- [ ] Add root-motion debug visualization
- [ ] Add root-motion determinism for replay
- [ ] Add validation of extracted motion against clip data

## Motion Matching

- [ ] Add a motion database built from clips
- [ ] Add pose and trajectory feature extraction
- [ ] Add a feature schema (foot positions, velocities, trajectory)
- [ ] Add custom user-defined features
- [ ] Add nearest-match query against the database
- [ ] Add trajectory prediction from input
- [ ] Add blending into the selected pose
- [ ] Add cost weighting per feature
- [ ] Add tag and constraint filtering of candidates
- [ ] Add database compression and acceleration structures
- [ ] Add continuity and responsiveness tuning
- [ ] Add pose-history and inertia handling
- [ ] Add a fallback to graph-based animation
- [ ] Add authoring and preview of motion databases
- [ ] Add data-capture tooling to grow the database
- [ ] Add motion-matching debug visualization
- [ ] Add quality and cost scaling
- [ ] Add determinism for replay and tests

## Morph Targets & Blend Shapes

- [ ] Add a morph-target asset with per-vertex deltas
- [ ] Add weighted morph application
- [ ] Add combined skinning and morph deformation
- [ ] Add GPU morph evaluation
- [ ] Add sparse morph storage for efficiency
- [ ] Add many simultaneous active morphs
- [ ] Add morph normal and tangent deltas
- [ ] Add curve-driven and animation-driven morph weights
- [ ] Add corrective morphs driven by pose
- [ ] Add in-editor sculpting of morph shapes
- [ ] Add morph groups, presets, and combinations
- [ ] Add morph LOD reduction with distance
- [ ] Add morph import from asset formats
- [ ] Add morph validation against the mesh
- [ ] Add morph debug inspection

## Facial Animation & Lip Sync

- [ ] Add a facial rig built on bones or blend shapes
- [ ] Add a facial control board abstraction
- [ ] Add expression presets and combinations
- [ ] Add emotion blending and layering
- [ ] Add viseme and phoneme-driven lip sync
- [ ] Add audio-driven mouth animation
- [ ] Add text-to-viseme generation
- [ ] Add eye look-at, saccades, and blink systems
- [ ] Add tongue and jaw controls
- [ ] Add curve-driven facial control values
- [ ] Add corrective shapes for extreme expressions
- [ ] Add facial-animation retargeting between characters
- [ ] Add facial-capture input and cleanup
- [ ] Add a facial pose library
- [ ] Add facial preview and control UI
- [ ] Add facial-animation validation

## Physics-Based Animation

- [ ] Add ragdoll setup from the skeleton
- [ ] Add blend from animation to ragdoll on death or impact
- [ ] Add blend from ragdoll back to animation (get-up)
- [ ] Add partial ragdoll for reactive limbs
- [ ] Add physical animation that drives bones toward animated targets
- [ ] Add hit reactions blended over locomotion
- [ ] Add spring-bone secondary motion (hair, cloth, accessories)
- [ ] Add cloth-simulation handoff for skinned garments
- [ ] Add per-bone physics blend weights
- [ ] Add collision handling during physical animation
- [ ] Add impulse and force application to driven bones
- [ ] Add stability and damping controls
- [ ] Add ragdoll joint limits authored from the rig
- [ ] Add physics-animation determinism options
- [ ] Add physics-animation debug visualization
- [ ] Add integration with the physics module

## Cinematics & Sequence Editor Core

- [ ] Add a multi-track cinematic sequence asset
- [ ] Add tracks bound to entities, cameras, and properties
- [ ] Add animation clips placed on tracks
- [ ] Add transform and property tracks with keyframes
- [ ] Add blending and crossfades between clips on a track
- [ ] Add sub-sequences nested inside a sequence
- [ ] Add a master timeline with playback and scrubbing
- [ ] Add frame-accurate evaluation and looping ranges
- [ ] Add spawnable objects created and destroyed by the sequence
- [ ] Add possessable bindings to existing scene objects
- [ ] Add per-track mute, solo, and lock
- [ ] Add folders and grouping of tracks
- [ ] Add markers, chapters, and labeled ranges
- [ ] Add an event track that fires gameplay and script calls
- [ ] Add audio and dialogue tracks synced to the timeline
- [ ] Add material, light, and post-process parameter tracks
- [ ] Add a visibility track to show and hide objects
- [ ] Add time dilation and slow-motion tracks
- [ ] Add a data-driven sequence asset format
- [ ] Add sequence versioning and migration
- [ ] Add deterministic sequence evaluation
- [ ] Add sequence playback off the main thread where possible

## Cinematic Cameras

- [ ] Add a cinematic camera with lens and sensor settings
- [ ] Add focal length, aperture, and focus-distance controls
- [ ] Add depth-of-field and bokeh tied to camera settings
- [ ] Add camera rigs (dolly, crane, rail, tripod)
- [ ] Add a rail and spline-follow camera
- [ ] Add look-at and target-tracking constraints
- [ ] Add camera shake and handheld noise
- [ ] Add a camera cut track for switching cameras
- [ ] Add smooth blends between cameras
- [ ] Add virtual-camera framing guides and composition overlays
- [ ] Add camera bookmarks and saved framings
- [ ] Add gameplay-to-cinematic camera handoff
- [ ] Add auto-framing and follow behaviors
- [ ] Add lens presets and real-world camera matching
- [ ] Add safe-area, grid, and aspect-ratio overlays
- [ ] Add camera-path preview and visualization
- [ ] Add camera-animation baking and export
- [ ] Add multi-camera preview thumbnails

## Cutscene Authoring & Flow

- [ ] Add cutscene sequences triggered by gameplay
- [ ] Add trigger volumes and script hooks to start cutscenes
- [ ] Add skippable cutscenes with clean state handoff
- [ ] Add interactive cutscenes with input prompts
- [ ] Add branching cutscenes based on state
- [ ] Add gameplay-to-cutscene and cutscene-to-gameplay transitions
- [ ] Add character possession and control during cutscenes
- [ ] Add letterboxing and cinematic UI toggles
- [ ] Add subtitle and dialogue synchronization
- [ ] Add localization of cutscene audio and subtitles
- [ ] Add save and resume across cutscenes
- [ ] Add a cutscene director for orchestrating actors
- [ ] Add fallback handling when a bound actor is missing
- [ ] Add cutscene preview from any point
- [ ] Add cutscene validation of bindings and triggers
- [ ] Add a cutscene flow graph linking sequences

## Recording, Takes & Motion Capture

- [ ] Add recording of gameplay and simulation into clips
- [ ] Add a take system with multiple recorded versions
- [ ] Add take naming, metadata, and organization
- [ ] Add recording of transforms, properties, and audio
- [ ] Add live motion-capture input streaming
- [ ] Add mapping of capture data onto a rig
- [ ] Add mocap import from standard formats
- [ ] Add mocap cleanup (jitter, foot slide, gaps)
- [ ] Add foot-lock and contact fixing on captured data
- [ ] Add retargeting of captured motion to project skeletons
- [ ] Add facial and finger capture support
- [ ] Add layering of captured and hand-keyed animation
- [ ] Add a review workflow for takes
- [ ] Add baking of takes to clip assets
- [ ] Add capture-session management and calibration
- [ ] Add capture and take diagnostics

## Cinematic Rendering & Export

- [ ] Add high-quality cinematic rendering mode
- [ ] Add a render queue for sequences
- [ ] Add frame-sequence image export
- [ ] Add movie-file export
- [ ] Add resolution, frame-rate, and aspect controls
- [ ] Add anti-aliasing and sampling overrides for renders
- [ ] Add motion-blur accumulation for offline quality
- [ ] Add render passes and layers export
- [ ] Add burn-in of timecode and metadata
- [ ] Add deterministic rendering for consistent takes
- [ ] Add batch rendering of multiple sequences
- [ ] Add render progress, cancel, and diagnostics

## Runtime Playback & Control

- [ ] Add a play, stop, and pause API
- [ ] Add crossfade between clips and states
- [ ] Add one-shot playback over a base pose
- [ ] Add montage-style slotted playback with sections
- [ ] Add section jumping and looping within a slot
- [ ] Add blend-in and blend-out per playback request
- [ ] Add interruption and priority between requests
- [ ] Add playback speed and time-scale control
- [ ] Add reverse and ping-pong playback
- [ ] Add per-slot masking so slots affect chosen bones
- [ ] Add queued and sequenced playback
- [ ] Add pose snapshot and freeze
- [ ] Add scripting API for animation control
- [ ] Add gameplay-driven parameter updates
- [ ] Add completion and interruption callbacks
- [ ] Add deterministic playback for replay

## Animation Events & Markers

- [ ] Add event markers on clip timelines
- [ ] Add duration event ranges with begin and end
- [ ] Add firing of events during playback
- [ ] Add event routing to gameplay and scripts
- [ ] Add footstep, sound, and effect events
- [ ] Add events that survive blending and interruption
- [ ] Add event tracks in the clip editor
- [ ] Add typed event payloads
- [ ] Add event suppression during fast blends
- [ ] Add event debug logging and visualization
- [ ] Add deterministic event firing for tests

## Sync Groups & Phase

- [ ] Add sync markers on clips for phase alignment
- [ ] Add sync groups with a leader and followers
- [ ] Add phase matching across blended locomotion clips
- [ ] Add automatic leader selection by weight
- [ ] Add normalized-time synchronization
- [ ] Add stride and cadence matching
- [ ] Add sync across state transitions
- [ ] Add sync-group debug visualization
- [ ] Add sync determinism for tests

## Mirroring

- [ ] Add mirror data mapping left and right bones
- [ ] Add mirrored pose evaluation
- [ ] Add mirrored clip playback
- [ ] Add automatic mirror-mapping generation from naming
- [ ] Add mirror-aware curves and events
- [ ] Add per-axis mirror configuration
- [ ] Add mirror preview and validation

## Import & Interchange Pipeline

- [ ] Add skeleton import from standard asset formats
- [ ] Add clip import with track extraction
- [ ] Add morph-target import
- [ ] Add automatic tangent-space and bind-pose handling
- [ ] Add retargeting on import to a project skeleton
- [ ] Add compression settings applied on import
- [ ] Add root-motion extraction options on import
- [ ] Add import of multiple clips from one file
- [ ] Add naming and mapping conventions on import
- [ ] Add scene and camera-animation import for cinematics
- [ ] Add export of clips, rigs, and cameras to interchange formats
- [ ] Add round-trip round-tripping with external tools
- [ ] Add import validation and error reporting
- [ ] Add re-import that preserves overrides
- [ ] Add import presets per content source
- [ ] Add batch import and export
- [ ] Add import and export diagnostics and previews

## Animation Compression

- [ ] Add keyframe reduction with an error threshold
- [ ] Add curve fitting and resampling
- [ ] Add per-bone compression settings
- [ ] Add rotation quantization
- [ ] Add constant-track collapsing
- [ ] Add relative-error metrics against the source
- [ ] Add compression presets by content type
- [ ] Add streaming-friendly compressed layouts
- [ ] Add decompression cost budgets
- [ ] Add compression quality diagnostics
- [ ] Add per-platform compression targets
- [ ] Add compression validation against tolerance

## Animation Streaming & LOD

- [ ] Add streaming of clips and libraries on demand
- [ ] Add bone-LOD that evaluates fewer bones at distance
- [ ] Add animation update-rate reduction with distance
- [ ] Add off-screen and dormant animation pausing
- [ ] Add interpolation to hide reduced update rates
- [ ] Add crowd-friendly aggressive animation LOD
- [ ] Add per-character LOD bias and forced LOD
- [ ] Add shared evaluation for identical crowd poses
- [ ] Add residency budgets for animation data
- [ ] Add LOD integration with the mesh LOD system
- [ ] Add streaming and LOD diagnostics
- [ ] Add validation of correctness across animation LODs

## ECS Integration & Bulk Animation

- [ ] Store animation state as components in chunk storage
- [ ] Add parallel pose evaluation across the worker pool
- [ ] Add SIMD-vectorized bone-matrix computation
- [ ] Add SIMD-vectorized pose blending over chunks
- [ ] Add job-graph scheduling of sample, blend, IK, and skin stages
- [ ] Add bulk evaluation of large crowds
- [ ] Add shared clip and skeleton data across instances
- [ ] Add zero-copy handoff of bone matrices to GPU skinning
- [ ] Add memory-traffic-aware batch sizes for evaluation
- [ ] Add deterministic parallel evaluation across thread counts
- [ ] Add instanced-crowd pose sharing and variation
- [ ] Add scaling to thousands of animated characters within budget
- [ ] Add throughput diagnostics for bulk animation
- [ ] Add a crowd-animation stress harness

## 2D & Sprite Animation

- [ ] Add sprite-sheet and flipbook animation
- [ ] Add frame timing and looping control
- [ ] Add atlas import and slicing
- [ ] Add cutout and bone-based 2D animation
- [ ] Add 2D skeletal skinning of sprite meshes
- [ ] Add 2D IK for limbs
- [ ] Add 2D mesh deformation and weighting tools
- [ ] Add 2D animation events and markers
- [ ] Add a 2D animation timeline editor
- [ ] Add blending between 2D animations
- [ ] Add 2D animation preview

## User-Friendly Authoring

- [ ] Make the animation editor usable immediately with no setup
- [ ] Add a one-click auto-rig for imported characters
- [ ] Add ready-to-use starter rigs and animation sets
- [ ] Add drag-and-drop of animations onto characters
- [ ] Add a large-icon, plain-language tool palette
- [ ] Add sensible defaults that produce good motion instantly
- [ ] Add a beginner mode that hides advanced controls
- [ ] Add guided workflows for rig, skin, and animate
- [ ] Add a template gallery of characters, rigs, and animations
- [ ] Add one-click "make it loop" and "clean up" actions
- [ ] Add one-click retarget onto any compatible character
- [ ] Add live preview of every change
- [ ] Add always-available undo, redo, and autosave
- [ ] Add friendly warnings with one-click fixes
- [ ] Add a motion library with searchable presets
- [ ] Add plain-language sliders (speed, intensity, smoothness)
- [ ] Add tooltips, hints, and a short interactive tutorial
- [ ] Add crash-safe recovery of in-progress work
- [ ] Add a distraction-free posing mode
- [ ] Add graphics-tablet and touch support for posing

## Animation Editor Shell & Workspace

- [ ] Add a dockable animation workspace layout
- [ ] Add synchronized time across all editor panels
- [ ] Add a viewport with posing manipulators and gizmos
- [ ] Add a playback toolbar with ranges, loop, and speed
- [ ] Add switching between rig, animate, and cinematic modes
- [ ] Add a channel and object outliner
- [ ] Add customizable panels and saved layouts
- [ ] Add a graph, dope-sheet, and non-linear editor tab set
- [ ] Add copy and paste across editor panels
- [ ] Add global undo and redo across all animation tools
- [ ] Add editor templates and starting workspaces
- [ ] Add a searchable command and node reference

## Debugging & Visualization

- [ ] Add skeleton and bone-axis rendering
- [ ] Add current-pose visualization
- [ ] Add active-state and transition display for the graph
- [ ] Add blend-weight readouts per node and layer
- [ ] Add IK goal and chain visualization
- [ ] Add constraint and rig visualization
- [ ] Add event-firing timeline overlay
- [ ] Add motion-trail visualization
- [ ] Add root-motion path visualization
- [ ] Add per-node evaluation-cost display
- [ ] Add a live parameter inspector
- [ ] Add a graph step-through for a captured frame

## Performance & Budgets

- [ ] Add per-character evaluation budgets
- [ ] Add parallel evaluation of independent characters
- [ ] Add update-rate and LOD-driven cost scaling
- [ ] Add pooling of evaluation buffers
- [ ] Add caching of unchanged sub-graph results
- [ ] Add GPU offload of skinning and morph work
- [ ] Add crowd batching and shared evaluation
- [ ] Add profiling and cost attribution per stage
- [ ] Add a headless animation benchmark harness
- [ ] Add machine-readable animation metrics for CI
- [ ] Add over-budget diagnostics with responsible characters

## Testing & Validation

- [ ] Add clip-sampling correctness tests
- [ ] Add blend and layer correctness tests
- [ ] Add state-machine transition tests
- [ ] Add IK convergence and stability tests
- [ ] Add retargeting fidelity tests
- [ ] Add root-motion extraction and application tests
- [ ] Add morph and skinning correctness tests
- [ ] Add compression error-tolerance tests
- [ ] Add determinism tests across runs and thread counts
- [ ] Add event-firing correctness tests
- [ ] Add sequence evaluation and binding tests
- [ ] Add golden-pose regression tests
- [ ] Add crowd-animation performance stress tests
- [ ] Add import round-trip validation tests

# 14 · Physics / Simulation

## Physics Core & World

- [ ] Add a 3D physics world driven by the Jolt backend
- [ ] Add a 2D physics world driven by the Box2D backend
- [ ] Add a fixed-timestep simulation loop with an accumulator
- [ ] Add configurable sub-stepping per frame
- [ ] Add a spiral-of-death clamp on catch-up steps
- [ ] Add gravity configuration per world and per region
- [ ] Add multiple independent physics worlds
- [ ] Add per-scene physics world ownership and lifecycle
- [ ] Add pause, resume, and single-step of the simulation
- [ ] Add a global time-scale that slows or speeds simulation
- [ ] Add deterministic stepping with a fixed order
- [ ] Add world creation, reset, and teardown
- [ ] Add world configuration presets (arcade, realistic, precise)
- [ ] Add broadphase configuration and tuning
- [ ] Add solver iteration and accuracy settings
- [ ] Add world-level statistics (bodies, contacts, islands)
- [ ] Add a unified 2D/3D world interface where sensible
- [ ] Add world serialization for deterministic restarts

## Backend Integration & Abstraction

- [ ] Wrap the Jolt 3D backend behind an engine-facing interface
- [ ] Wrap the Box2D 2D backend behind an engine-facing interface
- [ ] Add typed handles for bodies, shapes, and constraints
- [ ] Add lifetime and ownership management of backend objects
- [ ] Add a capability query for backend-specific features
- [ ] Add graceful handling of features one backend lacks
- [ ] Add backend allocator routing through the memory tracker
- [ ] Add backend job/threading integration with the worker pool
- [ ] Add backend version pinning and upgrade notes
- [ ] Add a debug-draw bridge from each backend
- [ ] Add configuration mapping from engine settings to each backend
- [ ] Add error and assertion routing into engine diagnostics
- [ ] Add backend profiling hooks
- [ ] Add validation that engine and backend state stay consistent

## ECS Runtime Integration

- [ ] Add a rigid-body component bound to a backend body
- [ ] Add collider components mapped to backend shapes
- [ ] Add a constraint component bound to a backend joint
- [ ] Add a trigger/sensor component
- [ ] Add a character-controller component
- [ ] Add creation and destruction of backend objects from component lifecycle
- [ ] Add transform sync from physics to entity transforms
- [ ] Add transform sync from entities to kinematic bodies
- [ ] Add interpolation of render transforms between fixed steps
- [ ] Add a stable entity-to-body mapping in both directions
- [ ] Add deferred physics structural changes through the command buffer
- [ ] Add parallel readback of simulation results across chunks
- [ ] Add batched application of forces and impulses
- [ ] Add SIMD-friendly layouts for physics-adjacent components
- [ ] Add scheduling of the physics step within the system scheduler
- [ ] Add ownership of static-body creation for colliders without a rigid body
- [ ] Add dirty tracking so only changed bodies re-sync
- [ ] Add bulk spawn and despawn of physics entities
- [ ] Add streaming activation and deactivation of bodies per world cell
- [ ] Add validation of entity/body consistency each frame

## Rigid Bodies 3D

- [ ] Add dynamic rigid bodies
- [ ] Add static bodies
- [ ] Add kinematic bodies driven by animation or code
- [ ] Add automatic mass computation from shape and density
- [ ] Add manual mass, center of mass, and inertia overrides
- [ ] Add linear and angular damping
- [ ] Add per-body gravity scale
- [ ] Add velocity get and set (linear and angular)
- [ ] Add force, torque, and impulse application
- [ ] Add impulse at a world point
- [ ] Add position and rotation teleport with velocity handling
- [ ] Add sleeping and automatic wake on interaction
- [ ] Add sleep thresholds and manual sleep control
- [ ] Add continuous collision detection for fast bodies
- [ ] Add motion-quality selection (discrete vs continuous)
- [ ] Add per-axis position and rotation locks
- [ ] Add maximum velocity and angular-velocity clamps
- [ ] Add kinematic-to-dynamic and back transitions
- [ ] Add per-body user data linking to the entity
- [ ] Add mass and inertia debug readouts
- [ ] Add scaling handling for shapes and inertia
- [ ] Add body activation and deactivation control

## Colliders & Shapes 3D

- [ ] Add a box collider
- [ ] Add a sphere collider
- [ ] Add a capsule collider
- [ ] Add a cylinder collider
- [ ] Add a tapered-capsule and cone collider
- [ ] Add a convex-hull collider
- [ ] Add automatic convex-hull generation from a mesh
- [ ] Add a compound collider of multiple shapes
- [ ] Add a triangle-mesh collider for static geometry
- [ ] Add a heightfield collider for terrain
- [ ] Add a plane collider
- [ ] Add per-shape local transform offsets
- [ ] Add non-uniform scale handling per shape
- [ ] Add shape margins and skin width
- [ ] Add a simple-vs-complex collision distinction
- [ ] Add convex decomposition for concave meshes
- [ ] Add shape caching and reuse across bodies
- [ ] Add collider validation (degenerate, inverted, too-small)

## Physics Materials 3D

- [ ] Add a physics-material asset
- [ ] Add static and dynamic friction
- [ ] Add restitution (bounciness)
- [ ] Add friction and restitution combine modes
- [ ] Add per-shape material assignment
- [ ] Add per-triangle materials on mesh colliders
- [ ] Add surface-type tags for footsteps and effects
- [ ] Add density used for automatic mass
- [ ] Add rolling and spinning friction
- [ ] Add material validation and defaults

## Collision Filtering 3D

- [ ] Add collision layers and object types
- [ ] Add a layer-versus-layer collision matrix
- [ ] Add per-body include and exclude masks
- [ ] Add collision groups for pair suppression
- [ ] Add sub-group filtering for articulated bodies
- [ ] Add query-only and simulation-only filter distinctions
- [ ] Add trigger-versus-solid response configuration
- [ ] Add named layers authored as an asset
- [ ] Add editor UI for the collision matrix
- [ ] Add validation of filter configuration
- [ ] Add runtime changes to a body's filters
- [ ] Add filter debug visualization

## Scene Queries 3D

- [ ] Add ray casts returning the closest hit
- [ ] Add ray casts returning all hits
- [ ] Add sphere casts and swept-sphere queries
- [ ] Add box and capsule sweeps
- [ ] Add convex-shape sweeps
- [ ] Add overlap tests for a shape at a pose
- [ ] Add point-inside and closest-point queries
- [ ] Add filtered queries by layer, mask, and tag
- [ ] Add query flags (static only, dynamic only, triggers)
- [ ] Add hit results with point, normal, distance, and material
- [ ] Add hit results with the struck entity and shape index
- [ ] Add back-face and initial-overlap handling
- [ ] Add batched queries for many rays
- [ ] Add async query submission and retrieval
- [ ] Add a query cache for repeated identical queries
- [ ] Add deterministic query ordering
- [ ] Add a scripting API for all query types
- [ ] Add query debug visualization

## Collision Events & Triggers 3D

- [ ] Add contact-begin, contact-stay, and contact-end events
- [ ] Add trigger and sensor overlap begin and end events
- [ ] Add contact points, normals, and separation data
- [ ] Add contact impulse and relative-velocity data
- [ ] Add filtering of which pairs report events
- [ ] Add per-body enable of contact reporting
- [ ] Add routing of events to gameplay and scripts
- [ ] Add deferred event dispatch drained once per frame
- [ ] Add contact modification callbacks before the solver
- [ ] Add one-shot and continuous event modes
- [ ] Add threshold filtering by impulse for impact sounds
- [ ] Add stable pair identity across frames
- [ ] Add event payloads with both entities and shapes
- [ ] Add suppression of self-collision events
- [ ] Add deterministic event ordering
- [ ] Add contact and trigger debug visualization

## Constraints & Joints 3D

- [ ] Add a fixed constraint
- [ ] Add a point (ball-socket) constraint
- [ ] Add a hinge constraint with an axis
- [ ] Add a slider (prismatic) constraint
- [ ] Add a cone-twist constraint
- [ ] Add a six-degrees-of-freedom constraint
- [ ] Add a distance constraint with min and max
- [ ] Add a spring-damper constraint
- [ ] Add a gear constraint
- [ ] Add a pulley and rack constraint
- [ ] Add a path/rail constraint
- [ ] Add angular and linear limits per axis
- [ ] Add motors and drives with target position and velocity
- [ ] Add drive stiffness, damping, and force limits
- [ ] Add breakable constraints with force and torque thresholds
- [ ] Add break events routed to gameplay
- [ ] Add soft and hard constraint modes
- [ ] Add constraint frames and local anchors
- [ ] Add collision enable/disable between constrained bodies
- [ ] Add runtime enable, disable, and retarget of constraints
- [ ] Add constraint solver-iteration overrides
- [ ] Add constraint debug visualization

## Constraint Authoring & Tools

- [ ] Add a constraint editor with gizmos
- [ ] Add interactive anchor and axis placement
- [ ] Add limit and cone visualization while editing
- [ ] Add drive and motor tuning UI
- [ ] Add breakable-threshold setup and preview
- [ ] Add snapping of anchors to bones and features
- [ ] Add constraint presets (door, wheel, rope, chain)
- [ ] Add copy and mirror of constraints
- [ ] Add validation of constraint configuration
- [ ] Add live preview of constrained motion

## Character Controller / Movement 3D

- [ ] Add a kinematic capsule character controller
- [ ] Add ground detection and grounded state
- [ ] Add slope-limit handling and sliding on steep surfaces
- [ ] Add step-up and step-down over small obstacles
- [ ] Add automatic stair traversal
- [ ] Add ceiling detection and head bonk handling
- [ ] Add wall sliding along surfaces
- [ ] Add snap-to-ground to stay on slopes and stairs
- [ ] Add crouch with capsule resize and clearance checks
- [ ] Add pushing of dynamic rigid bodies
- [ ] Add being pushed by moving and kinematic bodies
- [ ] Add riding of moving platforms with inherited velocity
- [ ] Add rotating-platform support
- [ ] Add collide-and-slide movement resolution
- [ ] Add penetration recovery and depenetration
- [ ] Add configurable skin width and contact offset
- [ ] Add a dynamic-body character mode as an alternative
- [ ] Add root-motion-driven movement reconciliation
- [ ] Add external forces and impulses on the controller
- [ ] Add gravity and custom up-vector support
- [ ] Add velocity, acceleration, and speed queries
- [ ] Add ground-normal and surface-type readback
- [ ] Add a scripting API for controller movement
- [ ] Add controller debug visualization

## Character Movement Modes 3D

- [ ] Add walking and running with acceleration curves
- [ ] Add jumping with variable height
- [ ] Add falling with air control
- [ ] Add coyote time and jump buffering
- [ ] Add swimming with buoyancy and drag
- [ ] Add flying and no-clip modes
- [ ] Add climbing and ledge handling
- [ ] Add mantling and vaulting helpers
- [ ] Add sprint, dash, and dodge helpers
- [ ] Add slope-speed adjustment
- [ ] Add configurable movement presets
- [ ] Add networked movement synchronization
- [ ] Add movement-mode transition events
- [ ] Add movement debug readouts

## Ragdoll 3D

- [ ] Add ragdoll body and constraint generation from a skeleton
- [ ] Add a physics-asset describing bodies, shapes, and joints
- [ ] Add automatic capsule fitting per bone
- [ ] Add joint-limit authoring per bone
- [ ] Add self-collision configuration
- [ ] Add activation of ragdoll on death or impact
- [ ] Add blend from animation into ragdoll
- [ ] Add blend from ragdoll back into animation (get-up)
- [ ] Add partial ragdoll for reactive limbs
- [ ] Add powered ragdoll driven toward an animated pose
- [ ] Add impulse application for hit reactions
- [ ] Add pose readback from ragdoll to the skeleton
- [ ] Add ragdoll sleeping and settling
- [ ] Add ragdoll LOD and dormancy at distance
- [ ] Add ragdoll authoring and preview tools
- [ ] Add ragdoll validation against the skeleton

## Cloth Simulation

- [ ] Add a cloth component with a simulation mesh
- [ ] Add particle and distance-constraint cloth solving
- [ ] Add bending and shear constraints
- [ ] Add pinning and attachment to bones and bodies
- [ ] Add wind and force response
- [ ] Add collision against capsules, spheres, and planes
- [ ] Add collision against the character body
- [ ] Add self-collision
- [ ] Add tearing and breakable cloth
- [ ] Add per-vertex stiffness and mass painting
- [ ] Add a paint tool for constraints and colliders
- [ ] Add cloth LOD and distance-based simplification
- [ ] Add GPU cloth solving where available
- [ ] Add skinned-to-cloth blend on the same mesh
- [ ] Add cloth-to-render-mesh skinning
- [ ] Add wind-source integration from the weather system
- [ ] Add cloth sleeping when at rest
- [ ] Add cloth authoring and preview
- [ ] Add cloth determinism options
- [ ] Add cloth cost budgets and diagnostics

## Destruction & Fracture

- [ ] Add a fracture authoring tool for meshes
- [ ] Add Voronoi-based fracturing
- [ ] Add clustered and hierarchical fracture levels
- [ ] Add a connection graph between chunks
- [ ] Add break-on-impact from contact impulse
- [ ] Add break-on-force and stress thresholds
- [ ] Add damage accumulation and propagation
- [ ] Add partial breakage revealing interior faces
- [ ] Add interior-material assignment on fracture
- [ ] Add debris spawning as rigid bodies
- [ ] Add debris lifetime, budgets, and cleanup
- [ ] Add radial and directional break forces
- [ ] Add force fields affecting broken pieces
- [ ] Add anchoring so structures stay until enough support breaks
- [ ] Add structural-support collapse simulation
- [ ] Add pre-fractured asset caching for performance
- [ ] Add runtime fracture for dynamic cuts
- [ ] Add streaming and pooling of debris
- [ ] Add destruction events routed to gameplay
- [ ] Add destruction LOD and distance culling
- [ ] Add fracture preview and tuning
- [ ] Add destruction determinism options

## Vehicle Physics

- [ ] Add a wheeled-vehicle simulation
- [ ] Add per-wheel suspension with spring and damper
- [ ] Add wheel raycast or shapecast ground contact
- [ ] Add tire friction with a friction model
- [ ] Add longitudinal and lateral slip
- [ ] Add an engine model with torque curve
- [ ] Add a gearbox with automatic and manual modes
- [ ] Add a clutch and drivetrain
- [ ] Add a differential (open, locked, limited-slip)
- [ ] Add steering with Ackermann geometry
- [ ] Add brakes and a handbrake
- [ ] Add downforce and aerodynamic drag
- [ ] Add anti-roll bars
- [ ] Add tracked-vehicle (tank) support
- [ ] Add motorcycle and two-wheeled balance
- [ ] Add wheel visual sync and steering animation
- [ ] Add surface-dependent traction
- [ ] Add vehicle reset and recovery
- [ ] Add vehicle input API and script control
- [ ] Add networked vehicle synchronization
- [ ] Add vehicle telemetry output
- [ ] Add vehicle debug visualization

## Vehicle Authoring & Tuning

- [ ] Add a vehicle setup asset
- [ ] Add wheel placement and configuration tools
- [ ] Add suspension and tire tuning UI
- [ ] Add engine and gearbox tuning UI
- [ ] Add a center-of-mass adjustment tool
- [ ] Add vehicle presets (sports car, truck, offroad, kart)
- [ ] Add live tuning while driving
- [ ] Add a telemetry graph panel
- [ ] Add validation of vehicle configuration
- [ ] Add a one-click drivable-vehicle setup

## Soft Body Simulation

- [ ] Add a soft-body volume simulation
- [ ] Add tetrahedral or shape-matching deformation
- [ ] Add pressure and volume preservation
- [ ] Add stiffness, damping, and plasticity controls
- [ ] Add collision with rigid bodies and the environment
- [ ] Add self-collision for soft bodies
- [ ] Add pinning and attachment points
- [ ] Add tearing and breaking of soft bodies
- [ ] Add skinning of a render mesh to the soft body
- [ ] Add soft-body LOD and simplification
- [ ] Add GPU soft-body solving where available
- [ ] Add soft-body authoring and preview
- [ ] Add soft-body sleeping and budgets
- [ ] Add soft-body determinism options

## Fluid Simulation

- [ ] Add a particle-based (SPH) fluid simulation
- [ ] Add configurable viscosity, density, and surface tension
- [ ] Add fluid containers and boundaries
- [ ] Add fluid interaction with rigid bodies and buoyancy
- [ ] Add fluid emitters and drains
- [ ] Add foam, spray, and bubble generation
- [ ] Add surface reconstruction for rendering
- [ ] Add fluid collision with the environment
- [ ] Add two-way coupling with rigid bodies
- [ ] Add grid-based fluid as an alternative solver
- [ ] Add GPU fluid solving where available
- [ ] Add flow, current, and force fields on fluid
- [ ] Add fluid LOD and particle budgets
- [ ] Add integration with the water surface system
- [ ] Add fluid authoring and preview
- [ ] Add fluid determinism options

## Particle Physics Simulation

- [ ] Add physics-driven particles with collision
- [ ] Add gravity, drag, and force response for particles
- [ ] Add particle-to-world collision with bounce and friction
- [ ] Add particle-to-particle interaction where affordable
- [ ] Add spawn from emitters with initial velocity
- [ ] Add lifetime, budgets, and pooling
- [ ] Add force-field and wind response
- [ ] Add GPU particle-physics solving
- [ ] Add handoff to and from the visual-effects system
- [ ] Add sub-stepping for fast particles
- [ ] Add particle-physics debug visualization
- [ ] Add particle-physics determinism options

## Forces, Fields & Effectors

- [ ] Add force regions applying directional force
- [ ] Add radial explosion forces with falloff
- [ ] Add wind zones affecting physics bodies
- [ ] Add buoyancy volumes with fluid density
- [ ] Add area gravity and gravity overrides
- [ ] Add area linear and angular damping
- [ ] Add vortex and turbulence fields
- [ ] Add drag and resistance volumes
- [ ] Add attractor and repulsor fields
- [ ] Add conveyor and surface-velocity effectors
- [ ] Add one-way and directional pass-through volumes
- [ ] Add field composition and priority
- [ ] Add impulse-on-enter and continuous-force modes
- [ ] Add scripting hooks for custom forces
- [ ] Add force-region authoring tools
- [ ] Add force and field debug visualization

## Rigid Bodies 2D

- [ ] Add dynamic 2D rigid bodies via Box2D
- [ ] Add static 2D bodies
- [ ] Add kinematic 2D bodies
- [ ] Add automatic mass from shape and density
- [ ] Add manual mass, center of mass, and inertia
- [ ] Add linear and angular damping
- [ ] Add per-body gravity scale
- [ ] Add velocity get and set
- [ ] Add force, torque, and impulse application
- [ ] Add fixed-rotation and locked-axis options
- [ ] Add bullet mode (continuous collision) for fast bodies
- [ ] Add sleeping and wake control
- [ ] Add teleport with velocity handling
- [ ] Add per-body user data linking to the entity
- [ ] Add body activation and deactivation
- [ ] Add 2D body debug readouts

## Colliders & Shapes 2D

- [ ] Add a box (polygon) collider
- [ ] Add a circle collider
- [ ] Add a capsule collider
- [ ] Add a convex-polygon collider
- [ ] Add an edge collider
- [ ] Add a chain collider for level boundaries
- [ ] Add a compound collider of multiple fixtures
- [ ] Add per-fixture local offsets
- [ ] Add one-way (platform) collision
- [ ] Add collider radius and skin controls
- [ ] Add automatic collider generation from sprites
- [ ] Add automatic collider generation from outlines
- [ ] Add shape caching and reuse
- [ ] Add 2D collider validation

## Joints 2D

- [ ] Add a revolute joint
- [ ] Add a prismatic joint
- [ ] Add a distance joint
- [ ] Add a weld joint
- [ ] Add a pulley joint
- [ ] Add a gear joint
- [ ] Add a motor joint
- [ ] Add a wheel joint for vehicles
- [ ] Add a friction joint
- [ ] Add a spring-damper joint
- [ ] Add a mouse/target joint for dragging
- [ ] Add joint limits and motors
- [ ] Add breakable 2D joints with events
- [ ] Add collision enable between jointed bodies
- [ ] Add runtime joint changes
- [ ] Add 2D joint debug visualization

## Scene Queries 2D

- [ ] Add 2D ray casts with closest and all hits
- [ ] Add 2D shape casts and sweeps
- [ ] Add 2D overlap tests
- [ ] Add 2D point queries
- [ ] Add AABB region queries
- [ ] Add filtered 2D queries by layer and mask
- [ ] Add hit results with point, normal, and fraction
- [ ] Add hit results with the struck entity and fixture
- [ ] Add batched 2D queries
- [ ] Add a scripting API for 2D queries
- [ ] Add deterministic 2D query ordering
- [ ] Add 2D query debug visualization

## Collision Events & Triggers 2D

- [ ] Add 2D contact begin, stay, and end events
- [ ] Add 2D sensor overlap events
- [ ] Add contact points, normals, and impulses in 2D
- [ ] Add pre-solve and post-solve callbacks
- [ ] Add per-fixture event enable
- [ ] Add routing of 2D events to gameplay and scripts
- [ ] Add deferred 2D event dispatch
- [ ] Add impulse-threshold filtering in 2D
- [ ] Add stable 2D pair identity
- [ ] Add self-collision suppression in 2D
- [ ] Add deterministic 2D event ordering
- [ ] Add 2D contact debug visualization

## Effectors & Areas 2D

- [ ] Add an area (buoyancy) effector
- [ ] Add a point effector with attraction and repulsion
- [ ] Add a platform effector for one-way and side-friction control
- [ ] Add a surface effector for conveyor motion
- [ ] Add a directional and constant-force effector
- [ ] Add a drag and damping area
- [ ] Add gravity overrides per area
- [ ] Add effector falloff and masks
- [ ] Add effector composition and priority
- [ ] Add effector authoring tools
- [ ] Add effector scripting hooks
- [ ] Add effector debug visualization

## 2D Character & Platformer Movement

- [ ] Add a 2D character-body controller
- [ ] Add ground, wall, and ceiling detection
- [ ] Add slope handling and slope-limit
- [ ] Add one-way platform drop-through
- [ ] Add moving-platform riding
- [ ] Add ladder and rope climbing
- [ ] Add coyote time and jump buffering in 2D
- [ ] Add variable jump height and double jump
- [ ] Add wall slide and wall jump
- [ ] Add dash and dodge helpers
- [ ] Add a 2D movement scripting API
- [ ] Add 2D controller debug visualization

## Tilemap & 2D World Physics

- [ ] Add collision generation from tilemaps
- [ ] Add composite and merged collider generation
- [ ] Add per-tile collision shapes and one-way flags
- [ ] Add automatic rebuild of tilemap collision on edit
- [ ] Add per-tile physics materials
- [ ] Add streaming of tilemap physics with the world
- [ ] Add optimization of large tilemap colliders
- [ ] Add tilemap collision debug visualization
- [ ] Add validation of generated tilemap collision
- [ ] Add one-way and platform tiles

## Simulation Stepping & Determinism

- [ ] Add a fixed-timestep step decoupled from frame rate
- [ ] Add sub-stepping with a maximum per frame
- [ ] Add interpolation of transforms between steps
- [ ] Add extrapolation as an alternative to interpolation
- [ ] Add deterministic body and contact ordering
- [ ] Add deterministic solver configuration
- [ ] Add seed control for any stochastic behavior
- [ ] Add cross-platform determinism validation
- [ ] Add async physics on a dedicated thread
- [ ] Add a synchronization point for gameplay reads
- [ ] Add rewind and re-simulation support
- [ ] Add snapshot and restore of full physics state
- [ ] Add step-cost budgets and adaptive sub-stepping
- [ ] Add determinism diagnostics and drift detection
- [ ] Add fixed-point option evaluation for strict determinism
- [ ] Add per-world stepping isolation

## Networking & Replication

- [ ] Add replication of rigid-body state
- [ ] Add client-side prediction of physics
- [ ] Add server-authoritative reconciliation
- [ ] Add snapshot interpolation for remote bodies
- [ ] Add deterministic lockstep simulation
- [ ] Add rollback and re-simulation on correction
- [ ] Add priority and relevancy for replicated bodies
- [ ] Add bandwidth-aware state compression
- [ ] Add ownership transfer of physics objects
- [ ] Add networked constraint and joint state
- [ ] Add networked destruction and break events
- [ ] Add anti-cheat validation of physics state
- [ ] Add networked-physics diagnostics
- [ ] Add replication tests across latency and loss

## Continuous Collision & Stability

- [ ] Add swept continuous collision for fast bodies
- [ ] Add speculative contacts to prevent tunneling
- [ ] Add penetration recovery with a bias
- [ ] Add contact-offset and skin-width tuning
- [ ] Add stacking stability tuning
- [ ] Add solver-iteration configuration for accuracy
- [ ] Add warm-starting of the solver
- [ ] Add restitution and friction stability at low speed
- [ ] Add jitter reduction and rest thresholds
- [ ] Add large-mass-ratio handling
- [ ] Add stability diagnostics and warnings
- [ ] Add stress-test scenes for stacking and chains

## Authoring & Editor Tools

- [ ] Add interactive collider editing with gizmos
- [ ] Add box, sphere, capsule, and hull fitting to a mesh
- [ ] Add one-click auto-collider generation
- [ ] Add convex-decomposition tooling with previews
- [ ] Add a collision-matrix and layer editor
- [ ] Add a physics-material editor and library
- [ ] Add ragdoll and physics-asset setup tools
- [ ] Add a vehicle setup workflow
- [ ] Add cloth and soft-body painting tools
- [ ] Add fracture authoring and preview
- [ ] Add drag-in-play manipulation of bodies
- [ ] Add a measure and mass-inspection tool
- [ ] Add snapping of colliders to geometry
- [ ] Add copy, mirror, and reuse of physics setups
- [ ] Add validation and fix-up of physics setups
- [ ] Add presets for common object types
- [ ] Add undo and redo across physics authoring
- [ ] Add a physics-setup gallery to learn from

## Debugging & Visualization

- [ ] Add collider wireframe rendering
- [ ] Add contact-point and normal visualization
- [ ] Add velocity and force vector visualization
- [ ] Add constraint and joint visualization
- [ ] Add sleeping and active-state coloring
- [ ] Add center-of-mass and inertia visualization
- [ ] Add query ray and sweep visualization
- [ ] Add trigger and overlap highlighting
- [ ] Add broadphase and island visualization
- [ ] Add a physics statistics HUD
- [ ] Add a per-body inspector
- [ ] Add a pause-and-step debugger for simulation
- [ ] Add slow-motion inspection
- [ ] Add a contact and event log
- [ ] Add capture and replay of a physics frame
- [ ] Add a screenshot-friendly clean physics overlay

## Performance, Budgets & Scaling

- [ ] Add a multi-threaded broadphase
- [ ] Add island-based parallel solving
- [ ] Add worker-pool integration for the solver
- [ ] Add sleeping and dormancy to skip idle bodies
- [ ] Add distance-based physics LOD and deactivation
- [ ] Add streaming activation of bodies per world cell
- [ ] Add per-frame simulation time budgets
- [ ] Add adaptive sub-stepping under load
- [ ] Add body and contact count budgets with warnings
- [ ] Add memory budgets and diagnostics for physics
- [ ] Add bulk-friendly data layouts for large body counts
- [ ] Add profiling and cost attribution per phase
- [ ] Add a headless physics benchmark harness
- [ ] Add machine-readable physics metrics for CI
- [ ] Add scaling to tens of thousands of bodies within budget
- [ ] Add over-budget diagnostics with responsible objects

## User-Friendly Authoring

- [ ] Make adding a collider and rigid body work with zero tuning
- [ ] Add automatic sensible mass and material defaults
- [ ] Add one-click ragdoll from a character
- [ ] Add one-click drivable vehicle from a mesh
- [ ] Add plain-language presets (bouncy, heavy, floaty, sturdy)
- [ ] Add auto-collider fitting on import
- [ ] Add friendly warnings with one-click fixes
- [ ] Add a beginner mode that hides advanced tuning
- [ ] Add live preview of physics behavior in the editor
- [ ] Add drag-and-drop physics presets
- [ ] Add guided setup for cloth, vehicles, and destruction
- [ ] Add a gallery of ready physics setups to reuse

## Testing & Validation

- [ ] Add rigid-body integration and determinism tests
- [ ] Add mass and inertia computation tests
- [ ] Add collision-shape correctness tests
- [ ] Add scene-query correctness tests for 3D
- [ ] Add scene-query correctness tests for 2D
- [ ] Add collision and trigger event tests
- [ ] Add constraint and joint stability tests
- [ ] Add breakable-constraint threshold tests
- [ ] Add character-controller movement tests (slopes, steps, platforms)
- [ ] Add 2D platformer movement tests
- [ ] Add ragdoll setup and blend tests
- [ ] Add cloth simulation and collision tests
- [ ] Add destruction and fracture tests
- [ ] Add vehicle simulation tests
- [ ] Add soft-body and fluid tests
- [ ] Add force-region and effector tests
- [ ] Add tilemap collision-generation tests
- [ ] Add ECS transform-sync and runtime-integration tests
- [ ] Add cross-platform determinism tests
- [ ] Add networked-physics prediction and reconciliation tests
- [ ] Add stacking and stability stress tests
- [ ] Add large-scale performance stress tests
- [ ] Add memory-budget and leak tests
- [ ] Add golden-scenario regression tests

# 15 · Input / Audio / Media

## Input Core & Devices

- [ ] Add a device abstraction over all input hardware
- [ ] Add keyboard device support
- [ ] Add mouse device support with buttons, movement, and wheel
- [ ] Add gamepad device support with buttons, sticks, and triggers
- [ ] Add touchscreen device support
- [ ] Add pen and stylus support with pressure and tilt
- [ ] Add motion-sensor support (accelerometer, gyroscope)
- [ ] Add XR controller device support
- [ ] Add device hotplug detection and reconnection
- [ ] Add per-device state snapshots each frame
- [ ] Add both event-driven and polled access
- [ ] Add raw and processed value access
- [ ] Add device capability queries
- [ ] Add device naming, ids, and product metadata
- [ ] Add unified button, axis, and vector value types
- [ ] Add timestamps on input events
- [ ] Add per-platform device backends behind the abstraction
- [ ] Add device debug inspection

## Action Mapping & Bindings

- [ ] Add named input actions with typed values
- [ ] Add action maps grouping related actions
- [ ] Add mapping contexts with priority stacking
- [ ] Add binding an action to multiple devices and keys
- [ ] Add composite bindings (2D vector from keys, 1D axis)
- [ ] Add control schemes per device class
- [ ] Add automatic scheme switching on device use
- [ ] Add context push, pop, and enable/disable
- [ ] Add per-context consumption so higher contexts block lower
- [ ] Add action phases (started, ongoing, performed, canceled)
- [ ] Add action value queries and event callbacks
- [ ] Add chorded and modifier-key bindings
- [ ] Add binding groups for platform-specific sets
- [ ] Add data-driven action and binding assets
- [ ] Add runtime creation and modification of bindings
- [ ] Add validation of action and binding configuration
- [ ] Add versioning and migration of input assets
- [ ] Add a scripting API for actions and values

## Input Processing

- [ ] Add deadzone processing for sticks and triggers
- [ ] Add sensitivity and scaling processors
- [ ] Add invert and clamp processors
- [ ] Add normalization and vector-magnitude processors
- [ ] Add smoothing and acceleration processors
- [ ] Add a press interaction
- [ ] Add a hold interaction with duration
- [ ] Add a tap and multi-tap interaction
- [ ] Add a slow-tap and long-press interaction
- [ ] Add a chord interaction
- [ ] Add input buffering with a configurable window
- [ ] Add repeat and echo handling
- [ ] Add custom processor and interaction plugins
- [ ] Add ordering of processors in a chain
- [ ] Add per-binding processor overrides
- [ ] Add processing debug visualization

## Rebinding & Accessibility

- [ ] Add runtime interactive rebinding
- [ ] Add listen-for-next-input capture
- [ ] Add conflict detection and resolution
- [ ] Add cancel and reset-to-default rebinding
- [ ] Add save and load of custom bindings
- [ ] Add binding presets and profiles
- [ ] Add exclusion of reserved keys from rebinding
- [ ] Add hold-versus-toggle accessibility options
- [ ] Add remapping for one-handed and alternative layouts
- [ ] Add input-assist options (auto-run, sticky modifiers)
- [ ] Add per-action sensitivity and deadzone in settings
- [ ] Add a rebinding UI with live capture
- [ ] Add validation and warnings for unbound actions
- [ ] Add accessibility presets

## Touch & Gestures

- [ ] Add multi-touch point tracking
- [ ] Add touch begin, move, stationary, and end phases
- [ ] Add tap and double-tap gestures
- [ ] Add long-press gestures
- [ ] Add swipe and fling gestures with direction
- [ ] Add pinch and zoom gestures
- [ ] Add rotate gestures
- [ ] Add pan and drag gestures
- [ ] Add gesture recognizers with priorities
- [ ] Add on-screen virtual joysticks
- [ ] Add on-screen virtual buttons and d-pads
- [ ] Add customizable on-screen control layouts
- [ ] Add touch-to-action binding
- [ ] Add touch debug visualization

## XR & Motion Controllers

- [ ] Add XR controller pose tracking
- [ ] Add XR controller buttons, triggers, and thumbsticks
- [ ] Add hand-tracking joint poses
- [ ] Add gesture recognition for hands
- [ ] Add gaze and head-pose input
- [ ] Add grip and aim pose distinction
- [ ] Add XR haptic output
- [ ] Add binding of XR input to actions
- [ ] Add controller model and pointer visualization
- [ ] Add interaction rays and selection
- [ ] Add XR input capability negotiation
- [ ] Add XR input debug visualization

## Haptics & Force Feedback

- [ ] Add gamepad rumble with low and high motors
- [ ] Add trigger haptics where supported
- [ ] Add haptic patterns and envelopes
- [ ] Add per-device haptic capability queries
- [ ] Add playback, layering, and stop of haptics
- [ ] Add intensity scaling and user settings
- [ ] Add XR controller haptics
- [ ] Add spatialized and directional haptics
- [ ] Add haptic asset authoring
- [ ] Add haptics debug and test tools

## Local Multiplayer & Device Assignment

- [ ] Add multiple local users
- [ ] Add device pairing and assignment per user
- [ ] Add join and leave flows for local players
- [ ] Add per-user action state and contexts
- [ ] Add device reassignment and reconnection handling
- [ ] Add split-screen input routing
- [ ] Add a lobby-style press-to-join flow
- [ ] Add per-user binding profiles
- [ ] Add unpaired-device handling
- [ ] Add local-user debug inspection

## Input Runtime & Integration

- [ ] Add input polling and dispatch within the frame loop
- [ ] Add window and application focus handling
- [ ] Add input consumption and priority between UI and gameplay
- [ ] Add routing of input to the UI system first
- [ ] Add input components in the ECS for gameplay
- [ ] Add per-frame action snapshots for systems
- [ ] Add fixed-step input sampling for deterministic gameplay
- [ ] Add input recording and playback
- [ ] Add input injection for automated testing
- [ ] Add a scripting API for input queries and events
- [ ] Add pause-and-resume of input capture
- [ ] Add input flushing on context changes
- [ ] Add latency measurement of input to action
- [ ] Add input event logging and diagnostics

## Input User-Friendly & Editor

- [ ] Add an action and binding editor
- [ ] Add a visual device-and-binding map
- [ ] Add live input preview and testing
- [ ] Add ready-made control-scheme presets
- [ ] Add automatic detection of the active device for prompts
- [ ] Add device-appropriate button glyphs and prompts
- [ ] Add plain-language binding labels
- [ ] Add a beginner-friendly default setup that just works
- [ ] Add warnings for missing or conflicting bindings
- [ ] Add a gallery of example input setups

## Audio Engine Core

- [ ] Add an audio engine on the miniaudio backend
- [ ] Add a dedicated audio mixing thread
- [ ] Add configurable sample rate and buffer size
- [ ] Add output-device selection and enumeration
- [ ] Add device hotplug and default-device following
- [ ] Add a master output with volume and mute
- [ ] Add a voice pool with a configurable limit
- [ ] Add voice priority and stealing
- [ ] Add virtualization of inaudible voices
- [ ] Add revival of virtual voices when audible again
- [ ] Add per-voice state (playing, paused, stopped, virtual)
- [ ] Add sample-accurate scheduling
- [ ] Add resampling for mismatched sample rates
- [ ] Add channel-count handling (mono, stereo, surround)
- [ ] Add clock and timeline for synchronized playback
- [ ] Add glitch and underrun detection
- [ ] Add engine start, stop, and reset
- [ ] Add thread-safe command submission to the audio thread
- [ ] Add memory accounting for audio
- [ ] Add engine statistics (active voices, CPU, memory)

## Sound Sources & Playback

- [ ] Add one-shot sound playback
- [ ] Add looping sound playback
- [ ] Add 2D non-spatialized sources
- [ ] Add 3D spatialized sources
- [ ] Add play, stop, pause, and resume
- [ ] Add per-source volume and pitch
- [ ] Add fade-in and fade-out
- [ ] Add start offset and seeking
- [ ] Add per-source priority
- [ ] Add playback rate and time-stretch
- [ ] Add randomized pitch and volume variation
- [ ] Add sound variation sets with weighting
- [ ] Add follow-entity sources that track a transform
- [ ] Add one-shot fire-and-forget helpers
- [ ] Add loop points and sustain regions
- [ ] Add playback completion callbacks and events
- [ ] Add source pooling and reuse
- [ ] Add source debug inspection

## Mixing & Buses

- [ ] Add a hierarchy of mixer buses and groups
- [ ] Add submixes feeding parent buses
- [ ] Add per-bus volume, pitch, and mute
- [ ] Add solo and bypass per bus
- [ ] Add send and return routing
- [ ] Add side-chain ducking (music under dialogue)
- [ ] Add mixer snapshots and transitions
- [ ] Add category buses (music, sfx, dialogue, ambience, ui)
- [ ] Add user volume settings per category
- [ ] Add automatic ducking rules
- [ ] Add metering per bus
- [ ] Add real-time parameters driving mix values
- [ ] Add a data-driven mixer asset
- [ ] Add mixer validation
- [ ] Add mixer authoring and preview
- [ ] Add mixer state save and restore

## 3D / Spatial Audio

- [ ] Add distance attenuation with configurable curves
- [ ] Add min and max distance and rolloff models
- [ ] Add stereo and surround panning by direction
- [ ] Add doppler-effect pitch shifting
- [ ] Add source spread and size
- [ ] Add a listener driven by the active camera
- [ ] Add multiple listeners for split-screen
- [ ] Add listener weighting for nearest-listener audio
- [ ] Add HRTF binaural spatialization
- [ ] Add ambisonic encoding and decoding
- [ ] Add ambient and directional bed handling
- [ ] Add air-absorption filtering over distance
- [ ] Add early reflections approximation
- [ ] Add velocity tracking for doppler
- [ ] Add per-source spatialization toggles
- [ ] Add attenuation-shape authoring (sphere, cone, box)
- [ ] Add spatialization debug visualization
- [ ] Add near-field and focus handling

## DSP & Effects

- [ ] Add a reverb effect
- [ ] Add a parametric equalizer
- [ ] Add a compressor and limiter
- [ ] Add a delay and echo effect
- [ ] Add low-pass, high-pass, and band-pass filters
- [ ] Add distortion and saturation
- [ ] Add chorus and flanger
- [ ] Add pitch shifting and formant control
- [ ] Add effect chains per source and per bus
- [ ] Add effect bypass and wet/dry mix
- [ ] Add real-time parameter control of effects
- [ ] Add a custom-DSP plugin interface
- [ ] Add effect ordering and routing
- [ ] Add effect presets
- [ ] Add convolution reverb from impulse responses
- [ ] Add a spectrum analyzer and metering
- [ ] Add effect cost budgets
- [ ] Add effect authoring and preview

## Occlusion & Propagation

- [ ] Add occlusion tests between source and listener
- [ ] Add obstruction handling for partial blocking
- [ ] Add low-pass filtering driven by occlusion
- [ ] Add reverb zones with blending
- [ ] Add portal-based sound propagation between rooms
- [ ] Add geometry-based acoustic approximation
- [ ] Add dynamic reverb from the surrounding space
- [ ] Add material-based absorption and transmission
- [ ] Add diffraction approximation around edges
- [ ] Add propagation cost budgets and throttling
- [ ] Add async occlusion queries
- [ ] Add reverb-zone authoring
- [ ] Add propagation debug visualization
- [ ] Add integration with the physics query system

## Audio Data & Banks

- [ ] Add an audio-clip asset with format metadata
- [ ] Add compressed and uncompressed clip support
- [ ] Add streaming of large clips from disk
- [ ] Add async decode off the audio thread
- [ ] Add sound banks grouping related clips
- [ ] Add bank load and unload on demand
- [ ] Add reference counting of loaded audio
- [ ] Add preloading and warmup of critical sounds
- [ ] Add per-platform compression and quality settings
- [ ] Add loudness normalization metadata
- [ ] Add memory budgets and residency for audio
- [ ] Add clip import and conversion
- [ ] Add clip validation
- [ ] Add streaming and bank diagnostics

## Interactive Music & Ambience

- [ ] Add layered music with independently controlled stems
- [ ] Add music transitions synced to bars and beats
- [ ] Add stingers and one-shot musical accents
- [ ] Add tempo, beat, and bar tracking
- [ ] Add horizontal re-sequencing of music segments
- [ ] Add vertical layering driven by gameplay intensity
- [ ] Add crossfades and quantized switches
- [ ] Add ambient beds with looping textures
- [ ] Add randomized ambient one-shots with timing rules
- [ ] Add ambience zones with blending
- [ ] Add real-time parameters driving music and ambience
- [ ] Add a music-state and transition graph
- [ ] Add music authoring and preview
- [ ] Add music and ambience validation

## Audio Middleware Integration

- [ ] Add an abstraction for external audio middleware
- [ ] Add loading of middleware banks
- [ ] Add posting of middleware events
- [ ] Add real-time parameters passed to middleware
- [ ] Add switches and states for middleware
- [ ] Add listener and source registration with middleware
- [ ] Add a fallback to the built-in engine when middleware is absent
- [ ] Add middleware profiling and diagnostics
- [ ] Add capability negotiation with middleware
- [ ] Add memory and voice accounting through middleware
- [ ] Add a consistent gameplay API across built-in and middleware
- [ ] Add validation of middleware integration

## Audio Runtime & Integration

- [ ] Add audio update within the frame loop
- [ ] Add a listener bound to the active camera
- [ ] Add audio-source components in the ECS
- [ ] Add transform-driven 3D source positions
- [ ] Add pause of gameplay audio on focus loss
- [ ] Add a scripting API for playback and parameters
- [ ] Add animation-event-driven sound triggers
- [ ] Add physics-impact-driven sound triggers
- [ ] Add occlusion updates throttled per frame
- [ ] Add voice and CPU budgets enforced at runtime
- [ ] Add time-scale and pause interaction with audio
- [ ] Add save and restore of audio settings
- [ ] Add runtime device-change handling
- [ ] Add audio runtime diagnostics

## Audio Authoring & User-Friendly

- [ ] Add a mixer editor with buses and sends
- [ ] Add an attenuation-curve editor
- [ ] Add one-click 3D sound setup with good defaults
- [ ] Add drag-and-drop sound assignment to objects
- [ ] Add plain-language controls (loudness, distance, echo)
- [ ] Add live preview and audition of sounds
- [ ] Add ready-made sound and mix presets
- [ ] Add a beginner mode hiding advanced routing
- [ ] Add warnings for clipping and missing sounds
- [ ] Add a sound-effect library browser
- [ ] Add sensible defaults so audio works immediately
- [ ] Add a gallery of example audio setups

## Video Decoding & Formats

- [ ] Add a video decoder abstraction
- [ ] Add support for common container formats
- [ ] Add support for common video codecs
- [ ] Add hardware-accelerated decoding where available
- [ ] Add a software-decode fallback
- [ ] Add decoding on a background thread
- [ ] Add frame queueing and pacing
- [ ] Add color-space conversion to renderable formats
- [ ] Add decode error handling and recovery
- [ ] Add decode performance diagnostics
- [ ] Add per-platform decoder backends
- [ ] Add decoder capability queries

## Media Playback & Control

- [ ] Add play, pause, stop, and resume
- [ ] Add seeking to a timestamp
- [ ] Add looping and playback-rate control
- [ ] Add rendering video to a texture
- [ ] Add rendering video into UI widgets
- [ ] Add rendering video onto materials and surfaces
- [ ] Add synchronized audio-track playback
- [ ] Add audio/video sync correction
- [ ] Add multiple simultaneous video players
- [ ] Add buffering and preload control
- [ ] Add playback state events and callbacks
- [ ] Add end-of-media and error events
- [ ] Add frame-accurate stepping
- [ ] Add a scripting API for media control

## Streaming Video & Subtitles

- [ ] Add streaming playback from local files
- [ ] Add streaming playback from network sources
- [ ] Add adaptive-bitrate handling for network streams
- [ ] Add buffering and rebuffering handling
- [ ] Add subtitle and caption rendering
- [ ] Add multiple subtitle tracks and selection
- [ ] Add multiple audio tracks and selection
- [ ] Add localization of subtitles
- [ ] Add subtitle timing and styling
- [ ] Add closed-caption accessibility support
- [ ] Add stream error recovery
- [ ] Add streaming diagnostics

## Cutscene & UI Video Integration

- [ ] Add a fullscreen movie player
- [ ] Add skippable video playback
- [ ] Add pre-rendered cutscene playback
- [ ] Add background and looping UI video
- [ ] Add video as an animated UI element
- [ ] Add transitions into and out of video
- [ ] Add input handling during video (skip, pause)
- [ ] Add handoff between video and gameplay
- [ ] Add video-in-world screens and displays
- [ ] Add cutscene-video validation

## Voice Capture & Encoding

- [ ] Add microphone capture
- [ ] Add capture-device selection and enumeration
- [ ] Add configurable sample rate and frame size
- [ ] Add Opus encoding of captured audio
- [ ] Add voice-activity detection
- [ ] Add push-to-talk mode
- [ ] Add open-mic mode with a threshold
- [ ] Add noise suppression
- [ ] Add echo cancellation
- [ ] Add automatic gain control
- [ ] Add input-level metering and monitoring
- [ ] Add a local test-your-mic loopback
- [ ] Add capture start, stop, and mute
- [ ] Add capture device hotplug handling
- [ ] Add capture diagnostics
- [ ] Add per-user capture settings

## Voice Transmission & Channels

- [ ] Add network transmission of encoded voice
- [ ] Add voice channels and rooms
- [ ] Add team and proximity channels
- [ ] Add positional (spatial) voice by speaker location
- [ ] Add proximity attenuation for spatial voice
- [ ] Add per-channel join and leave
- [ ] Add speaker priority and ducking of others
- [ ] Add mute and block per speaker
- [ ] Add bandwidth-aware bitrate adaptation
- [ ] Add packet-loss concealment
- [ ] Add server relay and peer-to-peer options
- [ ] Add integration with the networking layer
- [ ] Add transmission diagnostics
- [ ] Add per-channel policy configuration

## Voice Playback & Mixing

- [ ] Add per-speaker voice playback
- [ ] Add a jitter buffer for smooth playback
- [ ] Add spatialization of positional voice
- [ ] Add mixing of voice with game audio
- [ ] Add ducking of game audio under voice
- [ ] Add per-speaker volume and mute
- [ ] Add a speaking indicator and levels
- [ ] Add voice routing to the mixer buses
- [ ] Add late-packet and dropout handling
- [ ] Add playback diagnostics
- [ ] Add accessibility options for voice
- [ ] Add per-user playback settings

## Voice Moderation & Safety

- [ ] Add local mute and block lists
- [ ] Add server-side mute and ban
- [ ] Add reporting of abusive voice
- [ ] Add transcription hooks for moderation
- [ ] Add profanity and safety filtering hooks
- [ ] Add parental controls and age-gating
- [ ] Add default-safe settings for minors
- [ ] Add consent and privacy handling for capture
- [ ] Add audit logging for moderation actions
- [ ] Add moderation diagnostics

## Performance & Budgets

- [ ] Add a voice budget for concurrent audio
- [ ] Add DSP-cost budgets and throttling
- [ ] Add streaming and decode budgets for audio and video
- [ ] Add input-to-action latency measurement
- [ ] Add audio-thread load monitoring
- [ ] Add video-decode cost monitoring
- [ ] Add voice-chat bandwidth and CPU monitoring
- [ ] Add per-system profiling and attribution
- [ ] Add a headless media benchmark harness
- [ ] Add machine-readable media metrics for CI
- [ ] Add memory budgets across input, audio, and media
- [ ] Add over-budget diagnostics

## Testing & Validation

- [ ] Add action-mapping and binding tests
- [ ] Add rebinding and conflict-resolution tests
- [ ] Add interaction and processor tests
- [ ] Add touch-gesture recognition tests
- [ ] Add local-multiplayer device-assignment tests
- [ ] Add deterministic input recording and playback tests
- [ ] Add audio mixing and routing tests
- [ ] Add 3D spatialization and attenuation tests
- [ ] Add DSP-effect correctness tests
- [ ] Add occlusion and propagation tests
- [ ] Add audio streaming and bank tests
- [ ] Add interactive-music transition tests
- [ ] Add video-decode and playback tests
- [ ] Add subtitle and track-selection tests
- [ ] Add voice capture, encode, and transmit tests
- [ ] Add voice spatialization and mixing tests
- [ ] Add latency and budget stress tests
- [ ] Add cross-platform device and format tests
