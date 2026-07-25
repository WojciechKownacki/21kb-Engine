# Sprint 05 · Foliage / Vegetation / Scatter

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver a data-oriented vegetation system that supports authoring, procedural scatter, GPU-driven rendering and culling, streaming, interaction, lighting, ecosystem variation, and predictable large-world budgets.

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
