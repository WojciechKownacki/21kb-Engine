# Sprint 42 · Editor Tooling — Detailed Engineering Tasks

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Close concrete editor workflow and reliability gaps with transactional behavior, persistence, recovery, diagnostics, automation, live-code support, and end-to-end authoring verification.

## Transform Gizmos, Snapping & Viewport Manipulation

- [ ] Implement a gizmo coordinate-space toggle that switches axis orientation between world-aligned and the selected entity's local rotation, persisted per-viewport and bound to a hotkey, so translating along local axes follows the object's frame.
- [ ] Implement scale-value snapping in the gizmo drag path that rounds each axis' scale delta to a configurable increment, exposed as a scale-snap toolbar dropdown and verified by a self-test that a dragged scale lands exactly on the increment.
- [ ] Implement vertex and surface snapping that, while a modifier key is held during a translate drag, raycasts the mouse against other meshes' triangles and vertices and snaps the dragged object's pivot to the nearest hit, so parts assemble precisely without manual coordinate entry.
- [ ] Implement a pivot-mode switch between bounding-box center, object origin, and median of multi-selection that changes where the gizmo renders and about which point rotate and scale operate.
- [ ] Implement a numeric transform-entry overlay that lets the user type an exact delta mid-drag to move, rotate, or scale by that amount along the active gizmo axis, committing as a single undo step.
- [ ] Implement a viewport statistics HUD that overlays live frame time, FPS, draw-call count, triangle count, and visible-entity count sourced from the submission builder, toggled from the viewport toolbar.
- [ ] Implement camera bookmarks that store named yaw, pitch, pivot, and distance poses to the workspace file and restore them with an animated transition through the existing focus path.
- [ ] Implement a selection-isolation solo mode that temporarily hides all non-selected entities in the viewport render pass and restores full visibility on exit, driven from the hierarchy and reversible without mutating the scene document.
- [ ] Implement viewport-to-file capture that renders the current viewport at a chosen resolution and writes an image to disk, wired to a toolbar button and usable headlessly for regression screenshots.

## Command System & Undo/Redo

- [ ] Implement drag-coalescing in the command stack so consecutive same-target transform commands issued within one gizmo gesture merge into a single reversible entry, verified by a test asserting a multi-frame drag collapses to one undo.
- [ ] Implement an undo-history panel that lists every entry in the active partition with its label and a marker at the current position and lets the user click any entry to undo or redo to that exact point deterministically.
- [ ] Implement a can-merge-with protocol on editor commands plus a time-window threshold so rapid inspector field edits coalesce while distinct edits remain separate undo steps.
- [ ] Implement a transaction and scope API on the command stack that groups an arbitrary set of sub-commands into one atomic compound command whose undo reverses all children in reverse order.
- [ ] Implement a per-scene dirty-since-last-save indicator derived from comparing the command-stack position against the position recorded at save time, so the title bar and close prompt reflect true modification state across undo and redo.

## Asset Database & Content Browser

- [ ] Implement a persistent asset database that assigns a stable identifier to every project asset, stores a serialized index of path, type, hash, and dependencies on disk, and incrementally updates it on file change, replacing the current directory-scan index.
- [ ] Implement a cross-type reference and dependency inspector that, for any selected asset, queries the database to list all assets that reference it and all assets it depends on, generalizing the material-only reference finder.
- [ ] Implement safe asset rename and move that rewrites all incoming references by identifier across scenes, materials, and prefabs in one transaction so relocating an asset never produces dangling links.
- [ ] Implement a broken-reference scanner that walks the asset database, reports every asset with a missing dependency, and surfaces results in a dockable list with click-to-select.
- [ ] Implement a content-browser search-and-filter bar that queries the asset database by name substring and asset-type facet and repopulates the tile and list view live, independent of the current folder.
- [ ] Implement an import-preset system that stores per-extension import settings as project files and applies them automatically during import so re-imports are deterministic.
- [ ] Implement a background thumbnail-generation queue that renders mesh, material, and texture thumbnails off the UI thread and streams them into the disk cache as they complete so browsing a large folder never blocks paint.
- [ ] Implement drag-and-drop of a mesh into the viewport that spawns the entity at the surface point under the cursor via raycast rather than the origin, as a single undoable command.

## Prefabs & Scene Authoring

- [ ] Implement a prefab override system that records per-instance property deltas against the source prefab, displays overridden fields with a distinct marker in the inspector, and supports per-property revert-to-prefab and apply-to-prefab.
- [ ] Implement nested-prefab editing so a prefab instance can be opened in an isolated edit context, changes written back to the prefab asset, and all other instances updated on reload.
- [ ] Implement a prefab and scene diff view that compares two files or an instance against its source and lists added, removed, and changed entities and components in a reviewable tree.
- [ ] Implement prefab variants that inherit from a base prefab and store only their overrides so a family of related objects can share edits pushed from the base.
- [ ] Implement multi-entity copy, paste, and duplicate-with-hierarchy that serializes the selected subtree and reinstantiates it as one undoable command, including paste-into-parent.

## Console & Diagnostics

- [ ] Implement a console command-input line with a parser and a registry of named editor commands plus command-history recall, turning the read-only log into an interactive command surface.
- [ ] Implement a console text-search filter that highlights and narrows entries matching a query string, combined with the existing level filters.
- [ ] Implement click-to-navigate on console entries so a log line carrying a source location opens the script editor at that line or selects that entity.
- [ ] Implement per-category console filtering built from the distinct category values already stored on each entry so noisy subsystems can be muted.

## Profiling, Capture & Debugging

- [ ] Implement a CPU frame-profiler panel that samples named scopes per frame from the message loop and rendering submission and renders a per-frame timeline breakdown with min, average, and max timings.
- [ ] Implement a memory-profiler panel that tracks editor and scene allocations by category and displays live totals plus a high-water mark, sourced from an instrumented allocator hook.
- [ ] Implement a GPU frame-capture debugger that snapshots one frame's draw calls with their render state, textures, and view targets and presents an inspectable per-draw-call list for diagnosing rendering issues.
- [ ] Implement a render-pass visualization selector in the viewport that overrides output to show albedo, normals, depth, overdraw, or wireframe through the existing submission path.
- [ ] Implement an asset-load telemetry log that records every asset load and cook duration and cache hit or miss and exposes it in a sortable diagnostics list.

## Live-Coding & Hot-Reload

- [ ] Implement a Lua script hot-reload watcher that detects changes to script assets on disk and reloads them into the running scene session without restarting play mode, preserving entity state where possible.
- [ ] Implement native behaviour and plugin hot-reload that unloads and reloads the compiled gameplay module and re-binds live component instances so native behaviour changes take effect without relaunching the editor.
- [ ] Implement a live reimport-on-change pipeline that watches source files and re-cooks and reapplies them to open scenes automatically, extending the material-graph cook-reload behavior to all asset types.
- [ ] Implement play-mode frame stepping with single-step and step-N added alongside pause, advancing the simulation exactly one fixed tick per press for debugging gameplay frame-by-frame.

## Automation & Functional Test Harness

- [ ] Implement an input-driven automation harness that feeds synthetic pointer and keyboard event sequences into the live window message pipeline and asserts resulting editor state, enabling GUI-level regression tests beyond the object-level self-test.
- [ ] Implement an interaction record-and-playback system that captures a session's input events to a file and deterministically replays them against the editor so reported bugs can be reproduced in continuous integration.
- [ ] Implement a scriptable automation command layer that lets an external driver invoke editor operations over a local channel and read back results, enabling agent-driven end-to-end tests.
- [ ] Implement golden-image viewport comparison in the self-test harness that renders known scenes headlessly and diffs against stored reference images with a tolerance, failing on visual regressions.

## Persistence, Recovery & Version Control

- [ ] Implement periodic scene autosave that writes a timestamped recovery copy at a configurable interval and on focus loss and an on-launch recovery prompt that restores the latest copy after a crash.
- [ ] Implement version-control status integration that queries the working copy for each asset's state and overlays a status badge on content-browser tiles and hierarchy rows.
- [ ] Implement version-control actions to stage, revert, diff, and view history invocable from the asset context menu, operating through a pluggable backend interface so the concrete system is swappable.
- [ ] Implement a text-serialization canonicalization pass for scene and prefab files that produces stable, line-diffable output with sorted keys and deterministic ordering so merges and code review of scene changes are tractable.

## Workflow & Discoverability

- [ ] Implement a command palette invoked by a global shortcut that fuzzy-searches all named editor actions and assets and executes or opens the chosen result, providing keyboard-first navigation of the whole toolset.
- [ ] Implement a user-editable keymap that loads and saves keyboard bindings for every editor command to a config file and resolves conflicts, replacing the hard-coded shortcut policy.
- [ ] Implement a transient toast notification service that surfaces non-blocking success, warning, and error messages with auto-dismiss so feedback no longer relies solely on the console log.
- [ ] Implement entity tags and layers with a management panel, storing them on the scene document and enabling hierarchy filtering, bulk selection, and per-layer viewport visibility toggling.
