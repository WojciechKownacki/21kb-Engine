# Sprint 18 · AI / Navigation / Behavior / Perception / Crowd

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver scalable navigation, decision-making, perception, tactical reasoning, interaction, crowd behavior, authoring, debugging, and gameplay integration for individual agents and large simulated populations.

## Navigation Mesh & Generation

- [ ] Add a navigation mesh (navmesh): a polygon mesh of walkable surfaces generated from world geometry
- [ ] Add voxel rasterization of collision geometry for navmesh building
- [ ] Add walkable-surface filtering by slope and height
- [ ] Add region and contour generation from voxels
- [ ] Add convex-polygon and detail-mesh generation
- [ ] Add a tiled navmesh for large worlds
- [ ] Add per-tile build and streaming with the world
- [ ] Add navmesh generation from terrain and heightfields
- [ ] Add multiple navmeshes for different agent sizes
- [ ] Add agent radius, height, step, and slope parameters
- [ ] Add area types baked into the navmesh
- [ ] Add navigation-bounds volumes defining where to build
- [ ] Add exclude and include volumes
- [ ] Add offline baking of static navmesh
- [ ] Add runtime generation for dynamic worlds
- [ ] Add incremental rebuild of changed tiles
- [ ] Add navmesh serialization and streaming
- [ ] Add navmesh validation and gap detection
- [ ] Add a one-click bake with sensible defaults
- [ ] Add navmesh generation diagnostics

## Navigation Server & Runtime

- [ ] Add a central navigation service that owns navmeshes and queries
- [ ] Add path requests routed through the service
- [ ] Add synchronous and asynchronous path queries
- [ ] Add a query for the nearest point on the navmesh
- [ ] Add reachability and connectivity queries
- [ ] Add ray and walkability checks along the navmesh
- [ ] Add multiple navigation maps (2D and 3D)
- [ ] Add per-map configuration and layers
- [ ] Add thread-safe query submission from gameplay
- [ ] Add batched path queries across the worker pool
- [ ] Add a query cache for repeated requests
- [ ] Add navigation events surfaced to gameplay
- [ ] Add a scripting API for navigation queries
- [ ] Add navigation-service diagnostics

## Pathfinding Algorithms

- [ ] Add an A* pathfinder over the navmesh
- [ ] Add configurable heuristics and cost functions
- [ ] Add hierarchical pathfinding for long paths
- [ ] Add path smoothing with the funnel (string-pulling) algorithm
- [ ] Add any-angle pathfinding for open areas
- [ ] Add partial paths when the goal is unreachable
- [ ] Add path corridors that bound local steering
- [ ] Add path re-planning on navmesh change
- [ ] Add cost overrides and penalties per area
- [ ] Add waypoint and graph-based navigation as an option
- [ ] Add multi-goal and nearest-of-many queries
- [ ] Add flow-field pathfinding for many agents to one goal
- [ ] Add path budgets and time-sliced search
- [ ] Add deterministic pathfinding for replay and tests
- [ ] Add path caching and reuse
- [ ] Add pathfinding diagnostics

## Navigation Agents

- [ ] Add a navigation-agent component that moves along paths
- [ ] Add a move-to-location command
- [ ] Add a move-to-and-follow-actor command
- [ ] Add agent radius, height, and shape
- [ ] Add speed, acceleration, and turning limits
- [ ] Add stopping distance and arrival handling
- [ ] Add path following with corner cutting
- [ ] Add automatic re-path on blockage
- [ ] Add binding of an agent to a specific navmesh
- [ ] Add ground snapping and off-navmesh recovery
- [ ] Add pause, resume, and stop of movement
- [ ] Add velocity output consumed by movement and animation
- [ ] Add root-motion and physics-movement reconciliation
- [ ] Add agent state and events (moving, blocked, arrived)
- [ ] Add a scripting API for agent movement
- [ ] Add agent debug visualization

## Dynamic Navigation & Obstacles

- [ ] Add dynamic obstacles that carve the navmesh
- [ ] Add navigation modifiers that change area cost at runtime
- [ ] Add moving-obstacle handling with local avoidance
- [ ] Add door and gate navigation state
- [ ] Add dirty-region tracking for partial rebuilds
- [ ] Add async rebuild of affected navmesh tiles
- [ ] Add temporary blocking and unblocking of areas
- [ ] Add obstacle shapes (box, cylinder, convex)
- [ ] Add priority between carving and cost modifiers
- [ ] Add rebuild budgets to avoid frame spikes
- [ ] Add invalidation events to re-path affected agents
- [ ] Add dynamic-navigation diagnostics
- [ ] Add dynamic-navigation debug visualization
- [ ] Add validation of navmesh consistency after edits

## Off-Mesh Links & Traversal

- [ ] Add off-mesh links connecting disconnected navmesh areas
- [ ] Add jump and drop-down links
- [ ] Add ladder and climb links
- [ ] Add teleport and portal links
- [ ] Add automatic link generation from geometry
- [ ] Add hand-placed links in the editor
- [ ] Add smart links that trigger traversal animations
- [ ] Add per-link cost and agent filtering
- [ ] Add one-way and bidirectional links
- [ ] Add link entry and exit alignment
- [ ] Add link traversal events for gameplay and animation
- [ ] Add off-mesh-link debug visualization

## Local Avoidance & Steering

- [ ] Add reciprocal velocity-obstacle (RVO/ORCA) avoidance between agents
- [ ] Add a seek steering behavior
- [ ] Add flee and evade behaviors
- [ ] Add arrive with deceleration
- [ ] Add pursue and intercept behaviors
- [ ] Add wander and patrol behaviors
- [ ] Add separation, cohesion, and alignment (flocking)
- [ ] Add obstacle-avoidance steering
- [ ] Add wall-following and corridor-following
- [ ] Add priority and weighting between behaviors
- [ ] Add avoidance priority so important agents pass first
- [ ] Add crowd-aware avoidance quality levels
- [ ] Add avoidance of dynamic non-agent obstacles
- [ ] Add smoothing to avoid jitter and deadlocks
- [ ] Add deadlock detection and resolution
- [ ] Add avoidance layers and masks
- [ ] Add steering integration with animation and physics
- [ ] Add avoidance debug visualization

## Navigation Areas, Costs & Filters

- [ ] Add named navigation area types (default, water, road, danger)
- [ ] Add per-area traversal cost
- [ ] Add painting of areas onto the navmesh
- [ ] Add area assignment from surface materials and volumes
- [ ] Add query filters selecting allowed areas per agent
- [ ] Add per-agent cost multipliers
- [ ] Add temporary cost overrides (avoid fire, prefer cover)
- [ ] Add area-based include and exclude rules
- [ ] Add flags for jump-required and swim areas
- [ ] Add filter presets for common agent types
- [ ] Add area and cost debug visualization
- [ ] Add validation of area configuration

## Behavior Trees

- [ ] Add a behavior-tree asset and runtime
- [ ] Add sequence and selector composite nodes
- [ ] Add parallel composite nodes
- [ ] Add a random and weighted selector
- [ ] Add decorator nodes for conditions
- [ ] Add loop, cooldown, and time-limit decorators
- [ ] Add blackboard-based condition decorators
- [ ] Add task (leaf) action nodes
- [ ] Add latent tasks that run over multiple frames
- [ ] Add service nodes that tick while a branch is active
- [ ] Add lower-priority and self aborts
- [ ] Add observer-driven aborts from blackboard changes
- [ ] Add subtrees and reusable behavior modules
- [ ] Add dynamic subtree injection
- [ ] Add a task and node library
- [ ] Add custom task and decorator authoring
- [ ] Add per-node instance data
- [ ] Add tree evaluation off the main thread
- [ ] Add deterministic tree evaluation for tests
- [ ] Add tree hot-reload
- [ ] Add behavior-tree debugging (active path, node states)
- [ ] Add behavior-tree authoring in a visual editor

## Blackboard

- [ ] Add a typed blackboard of key-value data
- [ ] Add scalar, vector, entity, and enum key types
- [ ] Add per-agent blackboard instances
- [ ] Add a shared blackboard for groups
- [ ] Add key observers that notify on change
- [ ] Add default values and key metadata
- [ ] Add writing from perception and gameplay
- [ ] Add reading from behavior, utility, and planners
- [ ] Add blackboard serialization for save and network
- [ ] Add blackboard debugging and live inspection
- [ ] Add blackboard validation
- [ ] Add a scripting API for blackboard access

## Utility AI

- [ ] Add a utility-based decision system
- [ ] Add actions with scored considerations
- [ ] Add response curves mapping inputs to scores
- [ ] Add weighting and combination of considerations
- [ ] Add context and target scoring
- [ ] Add highest-score and weighted-random selection
- [ ] Add cooldowns and inertia to avoid thrashing
- [ ] Add data-driven utility definitions
- [ ] Add integration with behavior trees as a selector
- [ ] Add per-agent utility tuning
- [ ] Add authoring of considerations and curves
- [ ] Add utility scoring debug visualization
- [ ] Add deterministic utility evaluation
- [ ] Add utility validation

## Goal-Oriented Action Planning

- [ ] Add goals with desired world-state conditions
- [ ] Add actions with preconditions and effects
- [ ] Add a symbolic world-state representation
- [ ] Add a planner that searches action space with A*
- [ ] Add action costs and plan optimization
- [ ] Add dynamic replanning on world-state change
- [ ] Add goal selection and prioritization
- [ ] Add plan execution with per-action monitoring
- [ ] Add plan invalidation and recovery
- [ ] Add sensors feeding the world state
- [ ] Add data-driven goals and actions
- [ ] Add planning budgets and time-slicing
- [ ] Add plan debugging and visualization
- [ ] Add deterministic planning for tests

## Hierarchical Task Networks

- [ ] Add primitive and compound tasks
- [ ] Add methods that decompose compound tasks
- [ ] Add preconditions on methods and tasks
- [ ] Add a planner that builds a task hierarchy
- [ ] Add partial and re-planning support
- [ ] Add world-state integration with sensors
- [ ] Add domain authoring for task networks
- [ ] Add plan execution and monitoring
- [ ] Add planning budgets
- [ ] Add planning diagnostics and visualization

## AI State Machines

- [ ] Add a reusable AI finite state machine
- [ ] Add states with enter, update, and exit
- [ ] Add condition- and event-driven transitions
- [ ] Add a state stack for interruptions
- [ ] Add hierarchical AI states
- [ ] Add blackboard-driven transitions
- [ ] Add integration with behavior trees and utility
- [ ] Add state serialization for save and network
- [ ] Add state debugging and visualization
- [ ] Add deterministic state evaluation

## Perception System

- [ ] Add a perception component with configurable senses
- [ ] Add a sight sense with range, cone, and line-of-sight
- [ ] Add peripheral vision and central-focus falloff
- [ ] Add a hearing sense with radius and loudness
- [ ] Add a touch and collision sense
- [ ] Add a damage sense reacting to being hit
- [ ] Add a team and affiliation sense
- [ ] Add stimuli sources that emit sight, sound, and events
- [ ] Add line-of-sight checks against geometry
- [ ] Add detection accumulation over time (awareness meter)
- [ ] Add detection thresholds and states (unaware, suspicious, alert)
- [ ] Add stealth interaction (light, cover, noise, crouch)
- [ ] Add perception updates budgeted and time-sliced
- [ ] Add async line-of-sight queries
- [ ] Add perception events routed to behavior and blackboard
- [ ] Add a scripting API for perception
- [ ] Add perception debug visualization (cones, hearing radius)
- [ ] Add perception validation

## Sensory Memory & Awareness

- [ ] Add memory of perceived targets
- [ ] Add last-known-position tracking
- [ ] Add forgetting over time
- [ ] Add confidence and staleness of memories
- [ ] Add threat assessment and target selection
- [ ] Add group and shared perception memory
- [ ] Add investigation of last-known positions
- [ ] Add search behavior when a target is lost
- [ ] Add alertness propagation between agents
- [ ] Add reaction times and detection delays
- [ ] Add memory serialization for save and network
- [ ] Add awareness-state events for gameplay and UI
- [ ] Add memory debugging and inspection
- [ ] Add memory validation

## Environment Queries & Spatial Reasoning

- [ ] Add a spatial query system that scores locations and actors
- [ ] Add grid, ring, and points-around generators
- [ ] Add path-and-actor-based generators
- [ ] Add distance and dot-product tests
- [ ] Add line-of-sight and trace tests
- [ ] Add overlap and clearance tests
- [ ] Add navmesh-reachability tests
- [ ] Add scoring, weighting, and normalization of results
- [ ] Add best-single and best-N run modes
- [ ] Add async query execution and time-slicing
- [ ] Add query use inside behavior trees and utility
- [ ] Add data-driven query definitions
- [ ] Add caching of query results
- [ ] Add query authoring tools
- [ ] Add query debug visualization (scored points)
- [ ] Add deterministic query evaluation

## Influence Maps & Tactical Reasoning

- [ ] Add influence maps of threat, presence, and control
- [ ] Add propagation and decay of influence
- [ ] Add multiple layers (danger, allies, objectives)
- [ ] Add sampling of maps for decision inputs
- [ ] Add a cover system with cover points and quality
- [ ] Add tactical position selection (flank, retreat, advance)
- [ ] Add interaction points and usable objects for AI
- [ ] Add occupancy and reservation of positions and objects
- [ ] Add group tactical coordination
- [ ] Add influence-map budgets and resolution control
- [ ] Add influence and cover debug visualization
- [ ] Add tactical-reasoning authoring
- [ ] Add deterministic tactical evaluation
- [ ] Add tactical-reasoning validation

## Crowd Simulation

- [ ] Add a crowd manager coordinating many agents
- [ ] Add shared local avoidance across the crowd
- [ ] Add crowd flow and lane formation
- [ ] Add density-aware movement and slowdown
- [ ] Add goal and destination assignment for crowds
- [ ] Add spawning and despawning of crowd agents
- [ ] Add crowd navigation on the shared navmesh
- [ ] Add priority and politeness rules
- [ ] Add congestion and bottleneck handling
- [ ] Add ambient crowd behaviors (idle, wander, react)
- [ ] Add crowd reaction to events and hazards
- [ ] Add crowd LOD by distance and visibility
- [ ] Add crowd budgets and quality scaling
- [ ] Add crowd debug visualization
- [ ] Add crowd authoring and presets
- [ ] Add crowd validation

## Large-Scale / Mass AI

- [ ] Add a data-oriented agent representation in chunk storage
- [ ] Add processing of mass agents with SIMD kernels
- [ ] Add parallel agent updates across the worker pool
- [ ] Add behavior LOD (full behavior near, simplified far)
- [ ] Add update-rate LOD by distance and visibility
- [ ] Add representation LOD (full agent, proxy, static)
- [ ] Add flow-field navigation for huge agent counts
- [ ] Add shared and pooled behavior state
- [ ] Add spatial partitioning of mass agents
- [ ] Add promotion and demotion between detailed and mass agents
- [ ] Add bulk spawn and despawn through the command buffer
- [ ] Add zero-copy handoff of agent transforms to rendering
- [ ] Add memory-traffic-aware batch sizes
- [ ] Add deterministic parallel agent updates
- [ ] Add scaling to hundreds of thousands of agents within budget
- [ ] Add mass-AI throughput diagnostics
- [ ] Add a mass-AI stress harness
- [ ] Add mass-AI debug visualization

## Formations & Group Behavior

- [ ] Add formation definitions (line, column, wedge, circle)
- [ ] Add slot assignment within a formation
- [ ] Add leader-follower movement
- [ ] Add formation maintenance while moving and turning
- [ ] Add dynamic re-forming after obstacles
- [ ] Add group goals and coordinated tasks
- [ ] Add role assignment within a group
- [ ] Add squad-level decision making
- [ ] Add communication and shared blackboard for groups
- [ ] Add formation authoring and presets
- [ ] Add formation debug visualization
- [ ] Add formation validation

## AI Agent & Brain Components

- [ ] Add an AI brain component that drives an agent
- [ ] Add composition of navigation, perception, and behavior components
- [ ] Add agent lifecycle (spawn, activate, deactivate, destroy)
- [ ] Add per-agent configuration assets
- [ ] Add agent templates and archetypes
- [ ] Add agent teams, factions, and relationships
- [ ] Add agent difficulty and skill parameters
- [ ] Add agent state persistence for save and network
- [ ] Add agent messaging and events
- [ ] Add agent enable and disable by relevancy
- [ ] Add a scripting API for agent control
- [ ] Add agent debugging and inspection
- [ ] Add reusable agent presets
- [ ] Add agent validation

## AI Authoring & Editor Tools

- [ ] Add a behavior-tree visual editor
- [ ] Add a utility and consideration editor
- [ ] Add a planner domain editor for goals and actions
- [ ] Add a blackboard editor
- [ ] Add an environment-query editor
- [ ] Add navmesh build settings and preview in the editor
- [ ] Add area painting and link placement tools
- [ ] Add perception setup and preview
- [ ] Add crowd and formation authoring
- [ ] Add agent template and preset authoring
- [ ] Add live preview of AI in play
- [ ] Add copy, paste, and reuse of AI assets
- [ ] Add undo and redo across AI edits
- [ ] Add templates and starting AI setups
- [ ] Add a gallery of example AI to learn from
- [ ] Add AI-asset validation and warnings

## AI Scripting & Gameplay Integration

- [ ] Add a scripting API for custom tasks and decorators
- [ ] Add custom considerations and planner actions from script
- [ ] Add AI events consumable by gameplay
- [ ] Add gameplay commands to AI (move, attack, follow, flee)
- [ ] Add integration with the gameplay component library
- [ ] Add hooks into animation for AI-driven motion
- [ ] Add integration with dialogue and interaction systems
- [ ] Add data-driven AI configuration
- [ ] Add deterministic AI for replay and tests
- [ ] Add AI-scripting debugging
- [ ] Add hot-reload of AI scripts
- [ ] Add AI-scripting validation

## AI Debugging & Visualization

- [ ] Add a gameplay debugger overlay for selected agents
- [ ] Add navmesh and path visualization
- [ ] Add perception visualization (sight cones, hearing, stimuli)
- [ ] Add behavior-tree active-path and node-state display
- [ ] Add blackboard live values
- [ ] Add planner and plan visualization
- [ ] Add utility-score breakdown display
- [ ] Add environment-query scored-point display
- [ ] Add influence-map and cover visualization
- [ ] Add avoidance and steering vectors
- [ ] Add agent state and memory inspection
- [ ] Add per-agent AI timing and cost
- [ ] Add recording and replay of AI decisions
- [ ] Add filtering and selection of debugged agents
- [ ] Add a screenshot-friendly clean AI overlay
- [ ] Add step-through of AI decisions

## AI Performance & Scaling

- [ ] Add time-slicing of AI updates across frames
- [ ] Add per-agent and per-system update budgets
- [ ] Add distance- and visibility-based AI LOD
- [ ] Add parallel AI evaluation across the worker pool
- [ ] Add job-graph scheduling of perception, planning, and navigation
- [ ] Add pooling of AI state and query buffers
- [ ] Add caching of paths, queries, and line-of-sight
- [ ] Add async navigation and query execution
- [ ] Add dormancy for off-screen and distant agents
- [ ] Add memory budgets for AI data
- [ ] Add profiling and cost attribution per AI subsystem
- [ ] Add a headless AI benchmark harness
- [ ] Add machine-readable AI metrics for CI
- [ ] Add over-budget diagnostics with responsible agents

## AI User-Friendly Setup

- [ ] Add a one-click navmesh bake for a level
- [ ] Add automatic agent setup from a character
- [ ] Add ready-made behavior presets (patrol, guard, chase, flee, wander)
- [ ] Add drag-and-drop AI behaviors onto agents
- [ ] Add plain-language behavior and perception settings
- [ ] Add sensible defaults so an agent walks and reacts immediately
- [ ] Add a beginner mode that hides advanced tuning
- [ ] Add guided setup for navigation, perception, and behavior
- [ ] Add live preview and one-click test of AI
- [ ] Add friendly warnings with one-click fixes
- [ ] Add a gallery of example agents to open and tweak
- [ ] Add consistent, reversible AI authoring

## AI Testing & Validation

- [ ] Add navmesh generation correctness tests
- [ ] Add pathfinding correctness and determinism tests
- [ ] Add path-smoothing and corridor tests
- [ ] Add local-avoidance and deadlock tests
- [ ] Add off-mesh-link traversal tests
- [ ] Add behavior-tree execution and abort tests
- [ ] Add utility and planner determinism tests
- [ ] Add perception detection and line-of-sight tests
- [ ] Add memory and awareness-state tests
- [ ] Add environment-query correctness tests
- [ ] Add crowd flow and congestion tests
- [ ] Add mass-AI scale and performance stress tests
- [ ] Add formation-maintenance tests
- [ ] Add dynamic-navmesh rebuild tests
- [ ] Add save and network state tests for AI
- [ ] Add golden-scenario AI regression tests

## 3D & Volumetric Navigation

- [ ] Add volumetric navigation data for flying and swimming agents
- [ ] Add a sparse voxel octree for the 3D navigation space
- [ ] Add 3D pathfinding through open volumes
- [ ] Add navigation volumes that bound where 3D nav is built
- [ ] Add free-flight and tethered-flight movement
- [ ] Add swimming navigation within water bodies
- [ ] Add 3D local avoidance between flying agents
- [ ] Add height and ceiling constraints for 3D agents
- [ ] Add hybrid navigation switching between navmesh and volume
- [ ] Add links between ground and air navigation
- [ ] Add 3D path smoothing and corridors
- [ ] Add streaming and rebuild of volumetric nav data
- [ ] Add 3D navigation debug visualization
- [ ] Add 3D navigation tests

## Smart Objects & Environment Interaction

- [ ] Add smart objects that advertise available actions
- [ ] Add interaction slots with entry points and directions
- [ ] Add contextual animations bound to smart-object use
- [ ] Add querying of nearby smart objects by an agent
- [ ] Add filtering of objects by tags, needs, and conditions
- [ ] Add reservation and release of interaction slots
- [ ] Add multi-agent interactions on shared objects
- [ ] Add preconditions and gameplay effects on use
- [ ] Add navigation to and alignment with interaction slots
- [ ] Add interruption and abort of interactions
- [ ] Add smart-object authoring and setup
- [ ] Add runtime registration of smart objects
- [ ] Add smart-object debug visualization
- [ ] Add smart-object tests

## Combat & Tactical AI

- [ ] Add a combat behavior layer for engaging targets
- [ ] Add target selection and threat prioritization
- [ ] Add aiming with accuracy, spread, and skill
- [ ] Add weapon handling (fire, reload, switch)
- [ ] Add firing patterns (burst, suppressive, aimed)
- [ ] Add taking and using cover during combat
- [ ] Add peeking and blind-fire from cover
- [ ] Add flanking and advancing maneuvers
- [ ] Add retreat and disengage under pressure
- [ ] Add grenade and ability usage decisions
- [ ] Add reaction to incoming fire and suppression
- [ ] Add coordinated squad fire and movement
- [ ] Add engagement ranges and positioning
- [ ] Add difficulty tuning of combat skill
- [ ] Add combat-AI debug visualization
- [ ] Add combat-AI tests

## Vehicle & Traffic AI

- [ ] Add AI drivers that control vehicles
- [ ] Add path and racing-line following for vehicles
- [ ] Add speed control for corners and obstacles
- [ ] Add a road-network and lane graph for traffic
- [ ] Add lane following and lane changing
- [ ] Add intersection and right-of-way handling
- [ ] Add traffic rules (signals, signs, speed limits)
- [ ] Add obstacle and collision avoidance for vehicles
- [ ] Add pedestrian and cross-traffic yielding
- [ ] Add parking and pull-over behaviors
- [ ] Add traffic density and spawning management
- [ ] Add vehicle-AI difficulty and aggression
- [ ] Add vehicle and traffic debug visualization
- [ ] Add vehicle and traffic AI tests

## AI Director & Encounter Management

- [ ] Add an AI director that paces gameplay intensity
- [ ] Add intensity build-up and relaxation cycles
- [ ] Add dynamic difficulty adjustment
- [ ] Add encounter and spawn scheduling
- [ ] Add population budgets and caps
- [ ] Add spawn points and reinforcement waves
- [ ] Add relevancy-aware spawning around players
- [ ] Add despawn of irrelevant and distant agents
- [ ] Add encounter authoring and rules
- [ ] Add adaptive spawning from player performance
- [ ] Add director hooks for scripted moments
- [ ] Add director state persistence
- [ ] Add director debug visualization
- [ ] Add director tests

## NPC Schedules, Needs & Social AI

- [ ] Add daily schedules and routines for NPCs
- [ ] Add time-of-day-driven activity selection
- [ ] Add a needs model (hunger, rest, social, hygiene)
- [ ] Add utility-driven satisfaction of needs
- [ ] Add ownership of homes, jobs, and locations
- [ ] Add relationships and reputation between characters
- [ ] Add mood and emotion affecting behavior
- [ ] Add social interactions between NPCs
- [ ] Add knowledge and gossip spreading between agents
- [ ] Add reactions to player actions and world events
- [ ] Add ambient settlement-life behaviors
- [ ] Add schedule interruption and recovery
- [ ] Add data-driven routine and needs authoring
- [ ] Add schedule and needs persistence in saves
- [ ] Add social-AI debug visualization
- [ ] Add social-AI tests

## Dialogue, Barks & Conversation AI

- [ ] Add a bark system for contextual voice lines
- [ ] Add trigger conditions for barks (combat, idle, reaction)
- [ ] Add priority, cooldown, and deduplication of barks
- [ ] Add group and call-and-response barks
- [ ] Add conversation between two or more agents
- [ ] Add turn-taking and response selection
- [ ] Add interruption of conversation by events
- [ ] Add subtitle and localization hooks for lines
- [ ] Add lip-sync and facial-animation triggers
- [ ] Add data-driven line banks and rules
- [ ] Add bark and conversation debugging
- [ ] Add dialogue-AI tests

## Machine Learning & Learning Agents

- [ ] Add a learning-agent framework for trained behaviors
- [ ] Add sensor, action, and reward definitions
- [ ] Add reinforcement-learning training support
- [ ] Add imitation learning from recorded play
- [ ] Add a neural-network inference runtime
- [ ] Add on-device inference budgets and batching
- [ ] Add hybrid behaviors combining learned and scripted logic
- [ ] Add training-environment and episode management
- [ ] Add model versioning and hot-swap
- [ ] Add determinism and reproducibility controls
- [ ] Add safety and fallback when a model misbehaves
- [ ] Add learned navigation and locomotion policies
- [ ] Add learning-agent diagnostics
- [ ] Add learning-agent tests

## AI Motion & Locomotion Integration

- [ ] Add animation-driven movement for AI agents
- [ ] Add motion-matching selection for AI locomotion
- [ ] Add procedural locomotion for varied body types
- [ ] Add foot placement and IK on uneven ground
- [ ] Add turn-in-place and pivot handling
- [ ] Add speed and stride warping to match path speed
- [ ] Add contextual animation selection (walk, sneak, injured)
- [ ] Add smooth start, stop, and direction changes
- [ ] Add moving-platform and dynamic-surface handling
- [ ] Add root-motion-and-navigation reconciliation
- [ ] Add locomotion debug visualization
- [ ] Add locomotion-integration tests

## Reactions, Interrupts & Emotes

- [ ] Add a reaction system for immediate responses
- [ ] Add flinch, dodge, and stagger reactions
- [ ] Add take-cover reflexes under fire
- [ ] Add interruption of the current behavior by priority events
- [ ] Add resumption of interrupted behavior
- [ ] Add hit and damage reactions
- [ ] Add alert and startle reactions
- [ ] Add emotes and gestures for communication
- [ ] Add reaction cooldowns and blending
- [ ] Add reaction-system tests
