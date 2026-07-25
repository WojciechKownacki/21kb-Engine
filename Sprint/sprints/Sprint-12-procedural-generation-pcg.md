# Sprint 12 · Procedural Generation / PCG

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver a deterministic, extensible procedural-generation graph and runtime capable of producing reusable world, terrain, vegetation, geometry, structure, material, and gameplay content with non-destructive authoring and bounded execution.

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
