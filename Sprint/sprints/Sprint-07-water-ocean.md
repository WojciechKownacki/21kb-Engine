# Sprint 07 · Water / Ocean

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver authorable oceans, lakes, rivers, and waterfalls with scalable simulation, rendering, shoreline and terrain integration, underwater presentation, physics, navigation, weather response, diagnostics, and quality controls.

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
