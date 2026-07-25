# Sprint 33 · Editor Tooling, Profiling & Automation

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver extensible production tooling for live code, memory and frame analysis, trace inspection, automation, functional testing, source control, and failure-resistant editor customization.

## Editor Extensions & Custom Tools

- [ ] Add custom editor windows and panels from plugins
- [ ] Add custom property drawers per type in the inspector
- [ ] Add custom asset editors for new asset types
- [ ] Add menu, toolbar, and context-menu extension points
- [ ] Add asset context actions and right-click commands
- [ ] Add creation wizards and guided flows
- [ ] Add editor modes with dedicated tools and viewport interaction
- [ ] Add an editor scripting and automation API
- [ ] Add editor commands registerable by tools
- [ ] Add editor-tool packaging and distribution
- [ ] Add reload of editor tools without a restart
- [ ] Add discovery and enable/disable of editor extensions
- [ ] Add documentation and metadata for custom tools
- [ ] Add sandboxing so a broken tool cannot crash the editor

## Live Coding & Native Hot-Reload

- [ ] Add recompilation of changed native code while running
- [ ] Add patching of the running process with new code
- [ ] Add hot-reload of native gameplay and tool code
- [ ] Add preservation of live state across a code patch
- [ ] Add safe rollback when a patch fails to apply
- [ ] Add incremental compile for fast iteration
- [ ] Add function-level and object-level patching
- [ ] Add integration with external compilers and IDEs
- [ ] Add notification and status of live-coding sessions
- [ ] Add exclusion of code that cannot be safely patched
- [ ] Add live-coding diagnostics and logs
- [ ] Add a fallback full-rebuild path

## Memory Profiler

- [ ] Add memory snapshots of the whole process
- [ ] Add snapshot comparison and diff over time
- [ ] Add per-tag and per-subsystem memory breakdown
- [ ] Add per-asset and per-resource attribution
- [ ] Add allocation call-stack capture
- [ ] Add leak detection and unfreed-allocation reports
- [ ] Add a live memory usage graph
- [ ] Add allocation-count and fragmentation views
- [ ] Add integration with memory budgets and warnings
- [ ] Add export of memory reports
- [ ] Add remote memory capture from a running game
- [ ] Add memory-profiler diagnostics

## Frame & Render Debugger

- [ ] Add capture of a single rendered frame
- [ ] Add a draw-call list with step-through
- [ ] Add inspection of bound resources per draw
- [ ] Add inspection of render targets and intermediate buffers
- [ ] Add inspection of shaders, states, and bindings per draw
- [ ] Add resource-content viewing (textures, buffers)
- [ ] Add pixel history for a selected pixel
- [ ] Add per-draw and per-pass timing
- [ ] Add render-graph and pass visualization in the capture
- [ ] Add shader input and output inspection
- [ ] Add capture from a running device or remote build
- [ ] Add comparison of two frame captures
- [ ] Add export of captures for sharing
- [ ] Add integration with external graphics debuggers

## Unified Session Profiler & Trace Analysis

- [ ] Add recording of a trace session across subsystems
- [ ] Add correlated CPU, GPU, memory, and event timelines
- [ ] Add network and asset-load events in the trace
- [ ] Add loading and offline analysis of trace files
- [ ] Add zoom, filter, and search across a trace
- [ ] Add statistics, aggregation, and hot-path detection
- [ ] Add comparison of two sessions for regressions
- [ ] Add remote capture from a running game
- [ ] Add markers and annotations from gameplay
- [ ] Add continuous background tracing with a ring buffer
- [ ] Add export and sharing of trace sessions
- [ ] Add machine-readable trace output for CI

## Functional & Automation Testing

- [ ] Add a functional-test framework that drives the running game
- [ ] Add headless execution of automated tests
- [ ] Add scripted playthroughs with input injection
- [ ] Add assertions on game and world state
- [ ] Add screenshot and golden-image comparison tests
- [ ] Add soak and endurance test automation
- [ ] Add stress and load test scenarios
- [ ] Add performance-regression gates in automation
- [ ] Add replay-based deterministic tests
- [ ] Add editor UI automation tests
- [ ] Add multi-client and networked test orchestration
- [ ] Add test scheduling across devices and platforms
- [ ] Add test artifacts (logs, screenshots, traces, videos)
- [ ] Add flaky-test detection and quarantine
- [ ] Add continuous-integration orchestration and reporting
- [ ] Add a test dashboard with history and trends

## Version Control Depth

- [ ] Add adapters for distributed and centralized version-control backends
- [ ] Add checkout, add, delete, move, and revert of assets
- [ ] Add changelists and grouped submissions
- [ ] Add submit, sync, and update operations
- [ ] Add history, blame, and revision inspection
- [ ] Add binary-asset diff and visual comparison
- [ ] Add merge and conflict resolution for assets
- [ ] Add large-file and binary-asset handling
- [ ] Add exclusive checkout and lock status
- [ ] Add offline operation and later reconciliation

## Testing & Validation

- [ ] Add tests for editor commands and undo/redo integrity
- [ ] Add tests for custom-tool registration and sandboxing
- [ ] Add live-coding patch-and-rollback tests
- [ ] Add memory-profiler snapshot and diff tests
- [ ] Add frame-capture and inspection tests
- [ ] Add trace-recording and analysis tests
- [ ] Add version-control operation tests
- [ ] Add functional-test-framework self-tests
