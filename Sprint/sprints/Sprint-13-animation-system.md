# Sprint 13 · Animation System

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver an end-to-end animation pipeline covering import, rigging, authoring, runtime evaluation, blending, constraints, retargeting, deformation, cinematics, debugging, compression, streaming, and scalable crowd execution.

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

## Animation Graph Node Library

- [ ] Add a sequence-player node
- [ ] Add a sequence-evaluator node driven by an explicit time
- [ ] Add a random-sequence player node
- [ ] Add a blend-space player node
- [ ] Add a blend-space evaluator node
- [ ] Add a two-way blend node
- [ ] Add a multi-way blend node
- [ ] Add a blend-by-boolean node
- [ ] Add a blend-by-enum node
- [ ] Add a blend-by-integer node
- [ ] Add an apply-additive node
- [ ] Add a mesh-space additive node
- [ ] Add a make-dynamic-additive node
- [ ] Add a layered-bone-blend node
- [ ] Add a blend-bone-by-channel node
- [ ] Add a copy-pose-from-another-mesh node
- [ ] Add a reference-pose and identity-pose node
- [ ] Add a modify-bone node
- [ ] Add a rotate-root-bone node
- [ ] Add a slot node for layered playback requests
- [ ] Add a cached-pose node with named references
- [ ] Add a sub-graph and linked-layer node
- [ ] Add a call-function node that invokes graph functions
- [ ] Add a modify-curve node
- [ ] Add per-node relevancy and short-circuiting

## Inertialization & Transition Blending

- [ ] Add inertialization for source-free transition blending
- [ ] Add dead-blending as an alternative smoothing method
- [ ] Add per-bone inertialization
- [ ] Add curve and attribute inertialization
- [ ] Add inertialization requests raised by transitions
- [ ] Add configurable inertialization duration and easing
- [ ] Add pose-snapshot capture and blend-from-snapshot
- [ ] Add handling of teleports and discontinuities
- [ ] Add inertialization interaction with additive layers
- [ ] Add inertialization cost budgets
- [ ] Add inertialization debug visualization

## Animation Warping

- [ ] Add stride warping to match foot speed to movement speed
- [ ] Add orientation warping to lean animation toward the movement direction
- [ ] Add slope warping to adapt legs and body to terrain incline
- [ ] Add motion warping that aligns root motion to target points
- [ ] Add named warp targets updated from gameplay
- [ ] Add warp windows scoped to time ranges in a clip
- [ ] Add per-warp masks and per-bone influence
- [ ] Add curve-driven warp strength
- [ ] Add warping applied on top of the graph output
- [ ] Add combination of multiple warps in a defined order
- [ ] Add stability and clamping to avoid over-warping
- [ ] Add warping determinism for replay
- [ ] Add warp-target and warped-pose debug visualization

## Motion Trajectory & Locomotion

- [ ] Add sampling of past motion history into a trajectory
- [ ] Add prediction of a future trajectory from input and velocity
- [ ] Add smoothing and resampling of trajectories
- [ ] Add distance-to-marker matching for starts, stops, and pivots
- [ ] Add distance matching that syncs animation time to travelled distance
- [ ] Add turn-in-place detection and playback
- [ ] Add stride adjustment tied to movement speed
- [ ] Add a locomotion helper library of common nodes
- [ ] Add automatic blend-space sample placement from root-motion analysis
- [ ] Add foot-lock curves that pin feet during ground contact
- [ ] Add automatic foot-sync-marker generation
- [ ] Add ground alignment combining foot IK and slope warping
- [ ] Add a locomotion state model (idle, start, loop, stop, pivot)
- [ ] Add trajectory feeding into the motion-matching query
- [ ] Add locomotion-authoring previews
- [ ] Add trajectory and locomotion debug visualization

## Contextual & Interaction Animation

- [ ] Add multi-actor synchronized interaction scenes
- [ ] Add role assignment for participants in an interaction
- [ ] Add alignment of participants to an interaction anchor
- [ ] Add per-role animation and warp points
- [ ] Add synchronized start and phase across participants
- [ ] Add branching and variant selection within an interaction
- [ ] Add interruption and early-exit handling
- [ ] Add entry and exit transitions to and from gameplay
- [ ] Add attachment and prop handling during interactions
- [ ] Add networked synchronization of interactions
- [ ] Add contextual-interaction authoring tools
- [ ] Add validation of participant compatibility
- [ ] Add interaction debug visualization

## Data-Driven Animation Selection

- [ ] Add selection tables that map conditions to animations
- [ ] Add typed input columns (state, tags, floats, enums)
- [ ] Add output columns for animations, blend spaces, and assets
- [ ] Add first-match and weighted selection modes
- [ ] Add fallback rows for unmatched conditions
- [ ] Add nested and chained selection tables
- [ ] Add runtime evaluation from graph and gameplay
- [ ] Add integration with the motion-matching database
- [ ] Add hot-reload of selection tables
- [ ] Add authoring UI for selection tables
- [ ] Add validation of coverage and conflicts
- [ ] Add selection debug output

## Pose Assets & Drivers

- [ ] Add a pose-asset holding named poses
- [ ] Add a pose-by-name evaluation node
- [ ] Add weighted blending of multiple named poses
- [ ] Add curve-driven pose weights
- [ ] Add a radial-basis pose driver from bone transforms
- [ ] Add corrective poses driven by joint angles
- [ ] Add facial and expression pose sets
- [ ] Add extraction of poses from animation frames
- [ ] Add pose-asset authoring and preview
- [ ] Add pose-asset validation against the skeleton
- [ ] Add pose-driver debug visualization

## Custom Attributes & Curve Pipeline

- [ ] Add scalar animation curves alongside bone tracks
- [ ] Add per-bone custom attributes on clips
- [ ] Add per-pose custom attributes carried through evaluation
- [ ] Add typed attribute values (float, int, vector, string)
- [ ] Add curve metadata and naming
- [ ] Add a curve-source interface for external drivers
- [ ] Add bulk curve storage and evaluation
- [ ] Add a time-stretch curve for non-uniform retiming
- [ ] Add attribute and curve blending across layers
- [ ] Add gameplay data authored on the animation timeline
- [ ] Add curve and attribute filters
- [ ] Add attribute and curve debug inspection

## Animation Modifiers & Asset Processing

- [ ] Add reusable animation modifiers applied to clips
- [ ] Add automatic foot-sync-marker generation
- [ ] Add automatic foot-lock curve generation
- [ ] Add motion-curve extraction (speed, direction, distance)
- [ ] Add root-motion generation and cleanup modifiers
- [ ] Add curve creation and remapping modifiers
- [ ] Add notification and event insertion modifiers
- [ ] Add batch application across many clips
- [ ] Add re-application on re-import
- [ ] Add a modifier library and custom modifiers
- [ ] Add preview of modifier results
- [ ] Add validation and reporting for modifiers

## GPU Deformer Graph & ML Deformation

- [ ] Add a node-based GPU mesh-deformation graph
- [ ] Add a linear-blend-skinning deformer node
- [ ] Add morph and blend-shape deformer nodes
- [ ] Add cloth and simulation read-back deformer nodes
- [ ] Add spline, lattice, and cage deformer nodes
- [ ] Add custom compute-kernel deformer nodes
- [ ] Add mesh-deformer hooks with geometry read-back
- [ ] Add per-LOD deformer configuration
- [ ] Add alternate skin-weight profiles per LOD
- [ ] Add a machine-learning deformer that approximates high-fidelity results
- [ ] Add a training pipeline for ML deformers
- [ ] Add a fallback to standard skinning where unsupported
- [ ] Add deformer-graph authoring and preview
- [ ] Add deformer cost budgets and diagnostics
- [ ] Add deformer-graph debug visualization

## Vertex Animation & Crowd Baking

- [ ] Add baking of skeletal animation into vertex-position textures
- [ ] Add baking of animated normals into textures
- [ ] Add playback of vertex-animation textures on instanced meshes
- [ ] Add crowd rendering driven entirely by baked animation
- [ ] Add per-instance time offset and animation variation
- [ ] Add blending between baked animation states
- [ ] Add sharing and instancing of animation across many crowd actors
- [ ] Add a distance transition from baked to fully skinned
- [ ] Add a bake pipeline with resolution and quality settings
- [ ] Add memory and texture budgets for baked animation
- [ ] Add validation of baked-animation fidelity
- [ ] Add crowd-animation debug visualization

## Live Animation Streaming

- [ ] Add a live data-source abstraction for external capture
- [ ] Add subjects for skeletons, cameras, faces, and transforms
- [ ] Add real-time streaming into the running engine
- [ ] Add mapping of live subjects onto project rigs
- [ ] Add retargeting of live data to project skeletons
- [ ] Add interpolation and latency compensation for live data
- [ ] Add timecode synchronization across subjects
- [ ] Add recording of live streams into clips and takes
- [ ] Add multiple simultaneous live sources
- [ ] Add live facial and finger capture
- [ ] Add a live preview in the editor and in play
- [ ] Add reconnection and dropout handling
- [ ] Add live-stream diagnostics
- [ ] Add validation of subject-to-rig mapping

## Sequence Editor Advanced Tracks & Bindings

- [ ] Add an attach track that parents an object over time
- [ ] Add a path-follow track along a spline
- [ ] Add a constraint track (aim, position, parent) over time
- [ ] Add a console-variable track driven by the sequence
- [ ] Add a streaming-level and data-layer visibility track
- [ ] Add camera-shake source and trigger tracks
- [ ] Add material-parameter-collection tracks
- [ ] Add custom-primitive-data tracks
- [ ] Add typed property tracks (bool, enum, int, float, vector, color, rotation, string, object reference)
- [ ] Add dynamic bindings resolved at runtime
- [ ] Add binding overrides per instance
- [ ] Add marked frames and time retiming
- [ ] Add per-track and per-section conditions and variants
- [ ] Add layered animation mixing within the sequence
- [ ] Add pose-search and motion-matching tracks
