# Sprint 10 · Culling / Visibility / Occlusion

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver a unified CPU- and GPU-driven visibility pipeline that scales across views, shadows, instances, foliage, large worlds, gameplay queries, and supported rendering backends with measurable correctness and performance.

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
