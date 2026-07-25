# Sprint 04 · Terrain Editor

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver a non-destructive, scalable terrain authoring workflow that supports large worlds, procedural and manual editing, material and foliage integration, streaming, collision, navigation, validation, and production editor usability.

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
