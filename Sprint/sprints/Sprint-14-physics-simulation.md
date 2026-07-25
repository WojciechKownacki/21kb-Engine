# Sprint 14 · Physics / Simulation

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver stable and inspectable 3D and 2D physics foundations with engine-owned contracts for bodies, queries, contacts, constraints, characters, vehicles, deformables, simulation stepping, networking, authoring, and performance scaling.

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

## Collision Detection Pipeline

- [ ] Add a broadphase using an AABB tree
- [ ] Add a dirty-grid incremental broadphase for dynamic bodies
- [ ] Add a bounding-volume-hierarchy acceleration option
- [ ] Add a brute-force and particle-pair broadphase for small scenes
- [ ] Add narrowphase contact-generation dispatch
- [ ] Add a midphase for triangle-mesh contacts
- [ ] Add GJK distance queries between convex shapes
- [ ] Add EPA penetration-depth resolution
- [ ] Add SAT contact generation for boxes and convex shapes
- [ ] Add per-shape-pair contact-point generators
- [ ] Add contact-manifold generation and reduction
- [ ] Add redundant-contact pruning
- [ ] Add speculative contacts to prevent tunneling
- [ ] Add bit-based collision filtering in the broadphase
- [ ] Add incremental refit as bodies move
- [ ] Add collision-detection debug visualization

## Solver Internals & Islands

- [ ] Add a position-based-dynamics rigid solver
- [ ] Add a temporal Gauss-Seidel solver option
- [ ] Add island partitioning of interacting bodies
- [ ] Add parallel island grouping across the worker pool
- [ ] Add constraint-graph coloring for parallel-safe solving
- [ ] Add configurable position and velocity iteration counts
- [ ] Add warm-starting of the solver from the previous frame
- [ ] Add a moving simulation-space (relative-frame) mode
- [ ] Add structure-of-arrays body storage for cache efficiency
- [ ] Add deterministic body and constraint ordering
- [ ] Add sleeping and waking per island
- [ ] Add per-island solver-iteration overrides
- [ ] Add a solver validation and A/B harness
- [ ] Add solver-internals diagnostics

## Advanced Colliders & Implicit Shapes

- [ ] Add level-set (signed-distance) volumetric colliders
- [ ] Add skinned-mesh-driven colliders that deform with animation
- [ ] Add skinned level-set colliders
- [ ] Add implicit-shape union composition
- [ ] Add implicit-shape scaled and transformed wrappers
- [ ] Add implicit-shape intersection composition
- [ ] Add a bounding hierarchy over composed implicits
- [ ] Add runtime welding of bodies into one simulated proxy
- [ ] Add splitting of merged bodies back apart
- [ ] Add per-piece convex collision generation
- [ ] Add cooked collision-data caching
- [ ] Add tapered-capsule and tapered-cylinder shapes
- [ ] Add optional neural signed-distance colliders
- [ ] Add advanced-collider validation and preview

## Collision Channels, Presets & Profiles

- [ ] Add object channels describing what a body is
- [ ] Add trace channels for queries
- [ ] Add per-channel response (block, overlap, ignore)
- [ ] Add named collision presets combining object type and responses
- [ ] Add a collision-profile asset
- [ ] Add distinct query and simulation filtering
- [ ] Add per-shape channel overrides
- [ ] Add default presets for common object types
- [ ] Add a collision-preset editor
- [ ] Add validation of preset and channel setup
- [ ] Add migration of collision settings across versions
- [ ] Add a collision-response matrix visualization

## Physics Fields

- [ ] Add a physics-field system evaluated over bodies
- [ ] Add radial falloff fields
- [ ] Add plane and box falloff fields
- [ ] Add noise fields
- [ ] Add wave fields with configurable wave functions
- [ ] Add uniform and radial vector fields
- [ ] Add linear-force and linear-impulse targets
- [ ] Add angular-torque and angular-velocity targets
- [ ] Add linear-velocity and initial-velocity targets
- [ ] Add position and position-target drivers
- [ ] Add dynamic-state targets that switch kinematic and dynamic
- [ ] Add activation and disable-threshold targets
- [ ] Add sleeping-threshold targets
- [ ] Add kill and cull targets
- [ ] Add internal- and external-cluster-strain targets for fracture
- [ ] Add field operators (add, multiply, divide, subtract)
- [ ] Add field filters by object type and group
- [ ] Add field presets, authoring, and debug visualization

## Physics Interaction Components

- [ ] Add a grab handle that moves a body toward a target transform
- [ ] Add configurable handle stiffness, damping, and limits
- [ ] Add a spring component connecting two bodies
- [ ] Add a thruster component applying continuous force
- [ ] Add a radial-force and radial-impulse component
- [ ] Add an attractor and repulsor component
- [ ] Add a constraint component authored on entities
- [ ] Add a pickup-and-carry helper for gameplay
- [ ] Add a throw helper with inherited velocity
- [ ] Add a physics grab-and-manipulate gameplay helper
- [ ] Add a scripting API for interaction components
- [ ] Add interaction-component debug visualization

## Fracture Patterns & Authoring Tools

- [ ] Add uniform Voronoi fracturing
- [ ] Add custom Voronoi with user-placed sites
- [ ] Add radial and impact fracture patterns
- [ ] Add grid slice fracturing
- [ ] Add brick and bond patterns
- [ ] Add a single planar cut
- [ ] Add cutting by an arbitrary mesh
- [ ] Add automatic clustering into a hierarchy
- [ ] Add cluster-magnet and manual cluster tools
- [ ] Add a proximity and connection graph for connectivity
- [ ] Add anchoring so supported pieces stay in place
- [ ] Add embedding of geometry into the fracture hierarchy
- [ ] Add per-piece convex collision generation
- [ ] Add cleanup tools (fix tiny geometry, resample, recompute normals, auto-UV)
- [ ] Add a fracture-authoring mode with previews
- [ ] Add interior-material assignment on fracture

## Flesh & Muscle Simulation

- [ ] Add a tetrahedral FEM deformable simulation
- [ ] Add corotated and Neo-Hookean material models
- [ ] Add muscle activation with fiber directions
- [ ] Add a tetrahedral collection asset
- [ ] Add binding of skin to the deformable volume
- [ ] Add anatomical fat, muscle, and bone layering
- [ ] Add collision of flesh with rigid bodies
- [ ] Add self-collision for flesh
- [ ] Add stiffness, damping, and incompressibility controls
- [ ] Add flesh authoring and preview
- [ ] Add flesh LOD and cost budgets
- [ ] Add flesh determinism options

## Physics-Based Character Movement

- [ ] Add a dynamic-body character driven by physics
- [ ] Add a character-ground constraint for slope and step handling
- [ ] Add physics-based walking with acceleration and friction
- [ ] Add physics-based falling with air control
- [ ] Add physics-based flying and swimming modes
- [ ] Add crouch, jump, land, and launch handling
- [ ] Add water-entry detection and buoyant swimming
- [ ] Add ground-versus-air state detection
- [ ] Add path-following (spline, point, and route)
- [ ] Add reaction to impacts and external forces
- [ ] Add pushing and being pushed by dynamic bodies
- [ ] Add moving-platform support for physics characters
- [ ] Add a modular movement-mode framework with transitions
- [ ] Add networked movement prediction and reconciliation
- [ ] Add a choice between kinematic and physics character modes
- [ ] Add physics-character debug visualization

## Physical Animation Control

- [ ] Add physical-animation control driving bones toward an animated pose
- [ ] Add per-body and per-limb drive strength
- [ ] Add named control profiles (relaxed, braced, hit-reaction)
- [ ] Add blend weights between animation and physics per body
- [ ] Add smooth ramping of control strength over time
- [ ] Add impulse-driven hit reactions layered on animation
- [ ] Add masking so only chosen limbs go physical
- [ ] Add spring and damping control per body
- [ ] Add a control record for gameplay-driven changes
- [ ] Add a physics-with-control animation node
- [ ] Add integration with the animation graph output
- [ ] Add a runtime API for control profiles
- [ ] Add control-profile authoring and preview
- [ ] Add physical-animation-control debug visualization

## Physics Caching & Playback

- [ ] Add recording of physics simulation to a cache
- [ ] Add playback of a physics cache
- [ ] Add scrubbing and seeking within a cache
- [ ] Add caching of destruction and cloth results
- [ ] Add a cache asset format
- [ ] Add streaming of large caches
- [ ] Add blending from cache playback into live simulation
- [ ] Add deterministic re-recording
- [ ] Add cache compression
- [ ] Add interchange import and export of caches
- [ ] Add cache authoring and preview
- [ ] Add cache validation and diagnostics

## Buoyancy & Water Physics

- [ ] Add multi-point pontoon buoyancy on rigid bodies
- [ ] Add buoyancy sampled from the water surface
- [ ] Add a batched buoyancy manager for many floaters
- [ ] Add water drag and damping on submerged bodies
- [ ] Add water-current and flow forces on bodies
- [ ] Add floating stability and self-righting
- [ ] Add wave-driven bobbing tied to the water system
- [ ] Add splash and impact response entering water
- [ ] Add boat, raft, and buoy helpers
- [ ] Add enter-water and exit-water events
- [ ] Add buoyancy authoring and tuning
- [ ] Add buoyancy debug visualization

## Physics Recording & Visual Debugger

- [ ] Add recording of full physics state each frame
- [ ] Add replay and scrubbing of recorded physics
- [ ] Add inspection of bodies, shapes, and transforms in a recording
- [ ] Add inspection of contacts and contact points
- [ ] Add inspection of constraints and joints
- [ ] Add inspection of islands and solver state
- [ ] Add query and event overlays in the recording
- [ ] Add headless capture of physics recordings
- [ ] Add sharing and loading of recordings
- [ ] Add comparison of two recordings
- [ ] Add filtering and search within a recording
- [ ] Add export of recordings for bug reports

## Modular Vehicle Assembly

- [ ] Add component-based modular vehicle assembly
- [ ] Add an engine module with a torque curve
- [ ] Add clutch and transmission modules
- [ ] Add wheel and suspension modules
- [ ] Add aerofoil and downforce modules
- [ ] Add thruster and propulsion modules
- [ ] Add a chassis module with merged-body support
- [ ] Add wiring of modules into a drivetrain
- [ ] Add runtime attach and detach of modules
- [ ] Add per-module tuning and telemetry
- [ ] Add modular-vehicle presets
- [ ] Add modular-vehicle validation
