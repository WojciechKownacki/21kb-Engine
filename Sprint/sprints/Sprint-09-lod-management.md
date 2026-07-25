# Sprint 09 · LOD Management

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver unified level-of-detail generation, selection, transition, streaming, authoring, and budgeting across meshes, terrain, foliage, animation, materials, and hierarchical world representations.

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
