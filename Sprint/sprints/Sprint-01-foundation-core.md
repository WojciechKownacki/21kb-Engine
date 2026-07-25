# Sprint 01 · Foundation / Core

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Establish the shared runtime foundation, contracts, platform abstractions, diagnostics, memory model, concurrency primitives, serialization, configuration, reflection, and module boundaries required by every later engine sprint.

## Core / Foundation

- [ ] Add a shared `core` base module every subsystem depends on
- [ ] Add an open-addressing hash map and hash set (swiss / robin-hood)
- [ ] Add a small-buffer-optimized vector (SmallVector / InlineVector)
- [ ] Add fixed-capacity and stack-backed array/vector types
- [ ] Add intrusive linked list and intrusive hash map
- [ ] Add a custom string type with small-string optimization
- [ ] Add an interned string / string-ID table with reverse lookup
- [ ] Add a shared hashing header (FNV-1a, xxHash, hash_combine, byte hashing)
- [ ] Replace ad-hoc per-file hashing with the shared hashing header
- [ ] Add a compile-time stable `TypeId<T>()` facility
- [ ] Add a generic generational handle / slot-map template
- [ ] Add a generic `Result<T, E>` / `Expected` and error-category system
- [ ] Add engine-wide `Span` and `StringView` types
- [ ] Add bit utilities (bitset, enum flags, popcount/clz wrappers)
- [ ] Add UUID / GUID generation and parsing
- [ ] Add a tagged-variant helper with visitation

## Logging / Diagnostics / Assertions

- [ ] Add an engine-wide logging facility with levels and categories
- [ ] Add pluggable log sinks (console, file, editor console, debugger)
- [ ] Add async logging with rotation and unified formatting
- [ ] Add structured logging with key/value fields
- [ ] Add per-category runtime verbosity thresholds
- [ ] Add assertion macros (assert/check/ensure/verify) that are build-config aware
- [ ] Add a fatal-error handler with message and safe shutdown
- [ ] Add stack-trace capture with symbol resolution
- [ ] Add crash minidump writing for runtime, CLI, and hub
- [ ] Add a cross-platform crash handler
- [ ] Add a runtime in-game console with command entry
- [ ] Route all subsystem diagnostics through the unified logger

## Memory Management

- [ ] Add a linear / arena (bump) allocator
- [ ] Add a stack allocator with scoped markers
- [ ] Add a pool / block allocator for fixed-size objects
- [ ] Add a per-frame double-buffered frame allocator
- [ ] Add a general heap allocator wrapper with alignment support
- [ ] Add alignment helpers (AlignUp/AlignDown, aligned alloc/free)
- [ ] Add a memory tracker with tagged allocation categories
- [ ] Add per-subsystem memory budgets with over-budget diagnostics
- [ ] Add allocation statistics and telemetry (peak, live, count per tag)
- [ ] Add leak detection for debug builds
- [ ] Add STL allocator adapters that route through the tracker
- [ ] Add an intrusive ref-counted base and custom smart pointers
- [ ] Add a slot-map allocator with generational handle safety
- [ ] Route third-party allocators through the memory tracker

## Math / Geometry

- [ ] Add SIMD-accelerated Vec3/Vec4/Quat/Mat4 with scalar fallback
- [ ] Add a Transform type with compose, inverse, and interpolation
- [ ] Add Mat4 general inverse, TRS decompose, and orthonormalize
- [ ] Add perspective, orthographic, lookAt, and frustum matrix builders
- [ ] Add Sphere, OBB, Frustum, Capsule, Triangle, Segment, and Circle primitives
- [ ] Add ray intersection tests: AABB, Sphere, Triangle, OBB
- [ ] Add volume overlap tests: AABB-AABB, Sphere-Sphere, Frustum-AABB, Frustum-Sphere
- [ ] Add closest-point and distance queries (point-segment, point-AABB, segment-segment)
- [ ] Add spline types: Bezier, Catmull-Rom, Hermite, B-spline
- [ ] Add arc-length parameterization and spline path sampling
- [ ] Add an optional double-precision math variant for large worlds
- [ ] Add half-float (float16) conversion helpers
- [ ] Add swizzles and component-wise ops across Vec2/Vec3/Vec4
- [ ] Move duplicated renderer/editor matrix helpers onto the shared math library
- [ ] Move frustum/sphere culling primitives into the shared math library

## CPU / Threading / Job System

- [ ] Extract a general-purpose job system decoupled from ECS
- [ ] Add a lock-free work-stealing deque (Chase-Lev)
- [ ] Add fiber-based job execution with context switching
- [ ] Add job priorities and per-job continuations at the execution layer
- [ ] Add lock-free MPMC and SPSC queues
- [ ] Add a lock-free ring buffer
- [ ] Add a main-thread / render-thread dispatch queue
- [ ] Add an async task/future framework (task, when_all, when_any)
- [ ] Add parallel-for over arbitrary ranges outside ECS chunks
- [ ] Add an async I/O framework for asset and file loading
- [ ] Add thread naming and per-thread scratch context
- [ ] Implement the AVX2 and AVX-512 kernel backends
- [ ] Add a general SIMD kernel library reusable outside ECS transforms
- [ ] Add a cross-subsystem task-barrier abstraction

## Platform Abstraction Layer

- [ ] Add a central platform (HAL) module abstracting OS specifics
- [ ] Add a windowing abstraction with pluggable backends
- [ ] Add Linux (X11 / Wayland) window and input backends
- [ ] Add macOS window and input backends
- [ ] Add Android window, input, and lifecycle backends
- [ ] Add a virtual filesystem with mount points and archive/pak support
- [ ] Add async file I/O to the virtual filesystem
- [ ] Add a high-resolution clock and frame-timer abstraction
- [ ] Add an environment/paths service (exe, user data, config, cache, temp)
- [ ] Add a file/directory watcher (ReadDirectoryChanges / inotify / FSEvents)
- [ ] Add a subprocess spawning and pipe abstraction
- [ ] Add a CPU/platform info service (cores, cache line, page size, features)
- [ ] Promote dynamic library loading out of the module loader into the HAL
- [ ] Add a thread wrapper with naming, priority, and affinity
- [ ] Add power/battery and display-info queries for scalability

## Reflection / Type System / Metadata

- [ ] Add a global type registry for arbitrary C++ types, not just ECS components
- [ ] Add content-stable type IDs independent of registration order
- [ ] Add enum reflection with name-value tables
- [ ] Add reflection for nested structs and composite fields
- [ ] Add string, array, map, and handle/reference field types to reflection
- [ ] Add method/function reflection for native types
- [ ] Add attribute metadata (range, tooltip, category, transient, hidden)
- [ ] Add a codegen path to remove hand-written field registration
- [ ] Drive the editor inspector UI automatically from reflection metadata
- [ ] Drive serialization automatically from reflection metadata
- [ ] Add reflection-based property binding for scripting
- [ ] Add default-value and validation metadata per field

## Serialization

- [ ] Add a unified archive abstraction shared by all subsystems
- [ ] Add a JSON serialization backend for data and settings
- [ ] Add a diff-friendly, hand-editable text scene/prefab/resource format
- [ ] Add endian-portable binary serialization across all codecs
- [ ] Add schema evolution (tolerant add/remove/rename of fields)
- [ ] Extend the declarative migration framework beyond save games to all formats
- [ ] Add object-graph serialization with pointer/reference fixup
- [ ] Add cross-asset and cross-entity reference field serialization
- [ ] Extend the value model to strings, arrays, maps, nested structs, and handles
- [ ] Add reflection-driven automatic serialization for reflected types
- [ ] Add versioned per-component schema headers with upgrade hooks
- [ ] Add binary-to-text round-trip conversion for debugging

## Config / CVars / Settings

- [ ] Add a typed CVar registry of named runtime variables
- [ ] Add CVar get/set through the runtime console
- [ ] Add CVar overrides from command line, environment, and config file
- [ ] Add a hand-editable, diffable text config format (engine, user, project)
- [ ] Add layered settings tiers (engine, project, user, platform, command line)
- [ ] Add per-platform config overrides
- [ ] Add hot-reload of settings via the file watcher
- [ ] Add a unified command-line parser shared by all executables
- [ ] Add a `-set key=value` override pipeline into CVars/settings
- [ ] Add settings schema, validation, and migration for text config
- [ ] Auto-build the settings UI from reflection metadata
- [ ] Add user-preferences and editor-layout persistence

## Module / Plugin System

- [ ] Add external plugin manifest files (name, version, dependencies, content)
- [ ] Add plugin discovery/scan of plugin directories
- [ ] Add semver version constraints and compatibility checking
- [ ] Add optional vs required dependencies with failure isolation
- [ ] Add automatic hot-reload driven by a filesystem watcher
- [ ] Add an inter-module typed service/interface registry
- [ ] Harden the plugin ABI to a versioned struct-of-function-pointers boundary
- [ ] Add per-plugin content/asset mounting through the virtual filesystem
- [ ] Add an editor "reload plugin" action with state preservation
- [ ] Add plugin load-failure sandboxing so one plugin cannot crash the host
