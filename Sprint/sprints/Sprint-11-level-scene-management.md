# Sprint 11 · Level / Scene Management

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver versioned scene composition, asynchronous lifecycle management, nested content, play-mode transitions, validation, persistence, source-control-friendly authoring, and reliable loading for both small and streamed worlds.

## Scene Data Model & Format

- [ ] Add a scene asset holding a root hierarchy and entity list
- [ ] Add serialized component data per entity
- [ ] Add stable per-entity identifiers unique within a scene
- [ ] Add global identifiers for cross-scene references
- [ ] Add a scene dependency manifest (assets, other scenes, prefabs)
- [ ] Add per-scene settings (lighting, environment, physics, post-process)
- [ ] Add a distinction between embedded and referenced content
- [ ] Add scene bounds computed from content
- [ ] Add scene metadata (name, description, tags, author, timestamps)
- [ ] Add a binary scene format for fast loading
- [ ] Add a diff-friendly text scene format for source control
- [ ] Add conversion between binary and text formats
- [ ] Add a versioned scene format with migration
- [ ] Add integrity checksums and validation on load
- [ ] Add forward-compatible handling of unknown fields
- [ ] Add per-scene content manifests generated on save
- [ ] Add references from a scene to the assets it uses
- [ ] Add a stable reference type surviving rename and move

## Scene Loading Pipeline

- [ ] Add synchronous scene loading
- [ ] Add fully asynchronous loading off the main thread
- [ ] Add progress reporting with weighted stages
- [ ] Add dependency resolution and preloading before instantiation
- [ ] Add staged instantiation spread across frames to avoid hitches
- [ ] Add loading into a fresh world
- [ ] Add loading additively into the current world
- [ ] Add background deserialization of scene content
- [ ] Add staged GPU upload of scene assets
- [ ] Add job-system integration for parallel load work
- [ ] Add load priorities and queue ordering
- [ ] Add cancellation of in-flight loads
- [ ] Add load coalescing for duplicate requests
- [ ] Add retry and error handling for failed loads
- [ ] Add load-completion callbacks and events
- [ ] Add a placeholder or loading state while content instantiates
- [ ] Add hot-reload of a scene edited on disk
- [ ] Add deterministic load ordering for reproducibility
- [ ] Add a preload API that warms assets without instantiating
- [ ] Add memory-budget-aware loading

## Scene Unloading

- [ ] Add unloading of a loaded scene
- [ ] Add safe destruction of a scene's entities
- [ ] Add release of assets no longer referenced by any scene
- [ ] Add reference counting so shared assets survive
- [ ] Add deferred unload to the end of the frame
- [ ] Add unload events and completion callbacks
- [ ] Add partial unload of a region or layer within a scene
- [ ] Add cleanup validation that no dangling references remain
- [ ] Add async unload without frame stalls

## Multi-Scene & Composition

- [ ] Add additive loading of multiple scenes at once
- [ ] Add an active-scene concept for where new content is created
- [ ] Add moving entities between loaded scenes
- [ ] Add cross-scene references resolved as scenes load and unload
- [ ] Add a master/composition scene that references child scenes
- [ ] Add per-scene enable and disable
- [ ] Add scene ownership of the entities it created
- [ ] Add merging of several scenes into one
- [ ] Add splitting a scene into multiple scenes
- [ ] Add ordering and priority among loaded scenes
- [ ] Add conflict handling for identifiers across additive scenes
- [ ] Add a query for which scene an entity belongs to
- [ ] Add persistence of the active loaded-scene set
- [ ] Add lazy resolution of references to not-yet-loaded scenes

## Scene Lifecycle & Transitions

- [ ] Add a transition manager coordinating unload and load
- [ ] Add optional loading screens during transitions
- [ ] Add fade-out and fade-in transitions
- [ ] Add async pre-load of the destination before activation
- [ ] Add seamless handoff with no loading screen where possible
- [ ] Add persistent objects that survive a scene transition
- [ ] Add a minimum-display-time for loading screens
- [ ] Add cancelable and interruptible transitions
- [ ] Add transition events (started, progress, finished)
- [ ] Add ordered sequencing (fade, unload old, load new, fade back)
- [ ] Add a startup and splash flow before the first scene
- [ ] Add carry-over of state across transitions
- [ ] Add a fallback loading screen when streaming cannot keep up
- [ ] Add validation that transitions release the previous scene

## Sub-Scenes & Nested Scenes

- [ ] Add nested scene references embedded in a parent scene
- [ ] Add independent streaming of sub-scenes
- [ ] Add per-instance overrides on nested scenes
- [ ] Add lifecycle propagation to nested scenes
- [ ] Add cycle detection for nested scene references
- [ ] Add editing a nested scene in isolation
- [ ] Add depth limits and diagnostics for deep nesting
- [ ] Add stable references into nested scene content

## Editor Scene Management

- [ ] Add opening multiple scenes in the editor at once
- [ ] Add a scene tab or list for switching between open scenes
- [ ] Add selecting the active scene for new content
- [ ] Add a per-scene hierarchy panel
- [ ] Add drag-and-drop of entities between open scenes
- [ ] Add per-scene dirty tracking
- [ ] Add save, save-as, and save-all
- [ ] Add unsaved-changes prompts on close
- [ ] Add isolate or solo of a single scene in the viewport
- [ ] Add show and hide of individual open scenes
- [ ] Add a recent-scenes list
- [ ] Add close and reorder of open scenes
- [ ] Add creating a new empty scene
- [ ] Add duplicating an existing scene
- [ ] Add renaming a scene with reference fixup
- [ ] Add per-scene lock to prevent accidental edits
- [ ] Add indication of which scene owns the current selection
- [ ] Add reload-from-disk of a scene discarding edits

## Play Mode & Simulation

- [ ] Add entering and exiting play mode from the editor
- [ ] Add snapshotting the world before play begins
- [ ] Add restoring the exact pre-play state on exit
- [ ] Add play starting from the current scene
- [ ] Add play starting from the configured bootstrap scene
- [ ] Add pause and single-frame step in play mode
- [ ] Add isolation so play-mode changes never touch saved scenes
- [ ] Add a fast enter-play path that avoids a full reload
- [ ] Add a policy for edits made during play
- [ ] Add a deterministic world reset between play sessions
- [ ] Add simulate mode without possessing a player
- [ ] Add capture of play-mode state into a new scene

## Bootstrapping & Game Flow

- [ ] Add a configurable startup scene
- [ ] Add a boot sequence that runs before gameplay
- [ ] Add a persistent bootstrap scene that stays loaded
- [ ] Add a scene stack and history for navigation
- [ ] Add return-to-previous and return-to-menu flows
- [ ] Add a main-menu to gameplay flow
- [ ] Add a data-driven game-flow description
- [ ] Add flow events consumable by gameplay and scripts
- [ ] Add per-build overrides of the startup scene
- [ ] Add safe recovery when the startup scene is missing

## Scene References & Dependencies

- [ ] Add referencing scenes and content by stable identifier
- [ ] Add a dependency graph across scenes and assets
- [ ] Add missing-reference detection and reporting
- [ ] Add reference fixup when a target is renamed or moved
- [ ] Add a fallback for unresolved references
- [ ] Add a dependency browser in the editor
- [ ] Add detection of circular scene dependencies
- [ ] Add validation that all references resolve before save
- [ ] Add preloading of referenced scenes and assets
- [ ] Add reference-usage search across the project

## Scene Bounds, Metadata & Thumbnails

- [ ] Add automatic scene-bounds computation
- [ ] Add editable scene metadata fields
- [ ] Add scene thumbnail capture from a view
- [ ] Add scene statistics (entity count, memory, asset count)
- [ ] Add tags and categories for organizing scenes
- [ ] Add a scene browser showing thumbnails and metadata
- [ ] Add last-edited and authorship tracking

## Level Bake & Preprocessing

- [ ] Add association of baked lighting with a scene
- [ ] Add baked navigation data per scene
- [ ] Add baked occlusion and visibility data per scene
- [ ] Add baked reflection and light probes per scene
- [ ] Add a cook step that prepares a scene for shipping
- [ ] Add incremental rebake of only changed regions
- [ ] Add bake status and dirty tracking
- [ ] Add validation that baked data matches current content
- [ ] Add background baking that does not block editing
- [ ] Add bake artifacts stored alongside the scene

## Scene Validation

- [ ] Add pre-save scene validation
- [ ] Add detection of missing references
- [ ] Add detection of duplicate identifiers
- [ ] Add detection of orphaned and unreachable entities
- [ ] Add detection of content outside scene bounds
- [ ] Add checks for required components and settings
- [ ] Add one-click fixups for common problems
- [ ] Add a validation report panel
- [ ] Add customizable validation rules
- [ ] Add validation as a step in the build pipeline

## Scene Diff, Merge & Version Control

- [ ] Add a granular per-object file layout for scenes
- [ ] Add a text format that produces readable diffs
- [ ] Add diffing of two scene versions
- [ ] Add three-way merge of scene changes
- [ ] Add conflict detection and resolution
- [ ] Add visualization of scene changes in the editor
- [ ] Add version-control status per scene and per object
- [ ] Add locking of scenes or objects during team edits
- [ ] Add merge-friendly stable ordering of serialized content

## Persistence & Runtime State

- [ ] Add persistence of runtime changes to a scene
- [ ] Add save and restore of the active loaded-scene set
- [ ] Add scene state captured in save games
- [ ] Add per-scene reset to authored state
- [ ] Add a distinction between authored content and runtime changes
- [ ] Add versioned migration of persisted scene state

## Editor UX

- [ ] Add a guided new-scene wizard with templates
- [ ] Add scene templates and create-from-template
- [ ] Add a default scene for new projects
- [ ] Add autosave and crash-safe recovery of open scenes
- [ ] Add clear status of loaded, active, and dirty scenes
- [ ] Add plain-language prompts for save and discard
- [ ] Add drag-and-drop of scenes into the world
- [ ] Add a beginner-friendly single-scene mode
- [ ] Add a gallery of example scenes to open and learn from

## Performance & Large Scenes

- [ ] Add async loading and unloading that never stalls the editor
- [ ] Add load-time budgets and profiling per scene
- [ ] Add memory reporting per loaded scene
- [ ] Add handoff to world streaming for very large scenes
- [ ] Add progressive instantiation to smooth load spikes
- [ ] Add background prewarming of likely-next scenes
- [ ] Add diagnostics for slow-loading scenes

## Testing & Validation

- [ ] Add load and unload lifecycle tests
- [ ] Add additive multi-scene tests
- [ ] Add cross-scene reference resolution tests
- [ ] Add transition sequencing tests
- [ ] Add reference-fixup tests for rename and move
- [ ] Add binary and text round-trip serialization tests
- [ ] Add play-mode enter and restore fidelity tests
- [ ] Add scene-migration tests across versions
- [ ] Add save and restore fidelity tests for scene state
- [ ] Add large-scene load-performance stress tests
- [ ] Add validation-rule regression tests
