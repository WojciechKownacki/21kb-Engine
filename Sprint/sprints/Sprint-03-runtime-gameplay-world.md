# Sprint 03 · Runtime Gameplay / World

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver the core runtime world model for entities, components, scenes, prefabs, gameplay updates, scripting, messaging, persistence, and deterministic data-driven gameplay.

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
