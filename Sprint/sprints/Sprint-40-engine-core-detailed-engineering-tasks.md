# Sprint 40 · Engine Core — Detailed Engineering Tasks

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Close concrete implementation gaps in engine core, ECS, reflection, serialization, assets, asynchronous execution, and scripting hosts with code-level verification and no parallel source of truth.

## Memory & Custom Allocators

- [ ] Implement a linear/bump arena allocator that carves aligned sub-allocations from a single reserved block, supports rewind-to-marker and reset, never frees individual allocations, and is covered by tests asserting pointer monotonicity and correct alignment for over-aligned requests.
- [ ] Implement a double-buffered per-frame allocator that hands out transient memory during a frame and is fully reset in constant time at the frame boundary by flipping buffers, with a test proving frame-N allocations are invalidated and reused in frame N+2.
- [ ] Implement a fixed-block pool allocator backed by a free-list of recycled slots that returns constant-time allocate and free, grows by chunk when exhausted, and is validated by a random allocate/free stress test without corruption.
- [ ] Implement a scoped stack allocator with RAII markers that asserts LIFO discipline in debug builds and releases everything above the marker on scope exit.
- [ ] Implement an engine memory tracker that records live bytes and allocation counts per string or enum tag, enforces per-tag budgets by failing or asserting when a tag exceeds its budget, and exposes a snapshot report queryable per tag.
- [ ] Provide standard polymorphic-memory-resource adapters for the arena, pool, and frame allocators so existing container usage in hot paths can be redirected to engine allocators without changing container types.
- [ ] Implement an aligned system-allocation wrapper that guarantees 64-byte alignment for SIMD and cache-line data on all compilers and backs the archetype chunk pool's raw pages.
- [ ] Back the archetype chunk pool with a virtual-address reserve/commit allocator that reserves a large contiguous region up front and commits pages on demand, exposing committed-versus-reserved bytes and reducing per-chunk system allocation count to near zero under steady state.

## Threading, Jobs & Lock-Free Primitives

- [ ] Extract the worker pool and job graph into a subsystem-agnostic jobs module out of the ECS namespace so renderer, asset streaming, and scene systems can submit work to one shared scheduler, with a test submitting non-ECS jobs.
- [ ] Add a typed task/future primitive with continuation chaining that schedules the continuation on the worker pool when the antecedent completes, without blocking a worker thread, validated by a chained-computation test.
- [ ] Add job priority levels to the worker pool queues so latency-critical jobs preempt background work at steal time, with a test asserting a high-priority job scheduled after a low-priority backlog starts first.
- [ ] Rewrite the job-graph runtime state to use atomic per-node dependency counters and a lock-free ready queue so completion callbacks perform no heap allocation and can be called concurrently from multiple workers, verified by a multi-threaded stress test.
- [ ] Implement a bounded lock-free multi-producer multi-consumer queue using a sequence-numbered ring buffer, and use it as the worker pool's global submission queue, covered by a stress test checking no lost or duplicated items.
- [ ] Implement a wait-free single-producer single-consumer ring buffer for one-to-one handoff such as render-command streaming, with a throughput test across two pinned threads.
- [ ] Implement a guided parallel-for on the shared job system that splits a range into work-stealing sub-ranges sized from remaining iterations so uneven per-item cost self-balances, verified against static striping on a skewed workload.
- [ ] Add epoch-based reclamation or hazard pointers so lock-free structures can free retired nodes safely, with a test that reclaims memory only after all readers have advanced their epoch.

## SIMD & Math

- [ ] Replace the scalar loops in the kernel float lanes with real intrinsic specializations for SSE2, AVX2, AVX-512, and NEON covering load, store, arithmetic, min, max, compare, and select, gated by the existing backend tags, with a test asserting bit-identical results against the scalar fallback.
- [ ] Add a hardware fused-multiply-add path that emits the intrinsic when available and documents the precision difference from the separate multiply-add fallback, selectable via a determinism flag.
- [ ] Implement a SIMD batch transform routine that multiplies an array of matrices against an array of positions using the intrinsic lanes, exposed as a kernel and benchmarked to beat the scalar path on large arrays.
- [ ] Add cache-line-aligned SIMD value types to the math library with load and store helpers so structure-of-arrays math buffers can be consumed directly by intrinsic kernels.
- [ ] Add matrix inverse, transpose, and translation-rotation-scale decompose functions to the math library so transform back-solves do not depend on the renderer's third-party math.
- [ ] Add perspective, orthographic, and look-at matrix builders to the math library under the documented handedness convention, with tests round-tripping a projected point.
- [ ] Add frustum construction from a view-projection matrix plus box and sphere intersection tests so scene culling has a native, testable primitive independent of the renderer.
- [ ] Add a fixed-point deterministic math option for lockstep-simulation cases where floating-point rounding differences across machines are unacceptable, with a cross-configuration bit-equality test.

## ECS Storage, Archetypes & Command Buffer

- [ ] Add an archetype transition-edge cache that memoizes the destination archetype for each source-archetype and add/remove-component pair so repeated structural changes skip archetype re-resolution, verified by a test asserting the second identical add is constant time.
- [ ] Extend the command buffer to support non-trivially-copyable components by recording per-type move-construct and destroy function pointers alongside the byte payload, so components holding handles or strings can be deferred, verified by round-tripping a move-only component.
- [ ] Add a bulk move-entities-between-worlds operation that transfers whole chunks by pointer swap when component layouts match instead of per-entity copy, validated by a test asserting zero per-entity component copies.
- [ ] Add a defragment maintenance pass that compacts partially-filled chunks of the same archetype into fewer full chunks during a sync point, reported via maintenance stats, with a test asserting reduced chunk count and preserved data.
- [ ] Add an order-preserving stable-removal option to entity destruction for archetypes flagged order-sensitive, with a test proving remaining entities keep their relative row order.

## ECS Queries & Scheduler

- [ ] Add a persistent cached query object that stores its matched-archetype set and invalidates incrementally on structural version bump, so repeated per-frame iteration skips archetype matching when no archetype was created or destroyed.
- [ ] Add query result change-filtering that iterates only chunks whose component version exceeds a caller-held baseline using the existing dirty ranges, with a test asserting untouched chunks are skipped.
- [ ] Add automatic per-system entity command buffers that the scheduler allocates, passes to each parallel worker, and plays back deterministically at the stage's sync point, so systems can request structural changes during parallel iteration without manual buffer wiring.
- [ ] Add work-stealing at query-chunk granularity within a single system so one wide system's chunks are distributed across all idle workers, verified by telemetry showing more than one worker active for a single system.

## World Resources, Relations & Lifecycle

- [ ] Add world-resource singleton components stored once per world outside archetype storage so global state such as time, input, and config is accessible to systems without a carrier entity, with get, set, has, and remove tests.
- [ ] Add relation cleanup policies that run when a relation target entity is destroyed so dangling relation pairs cannot survive, verified by destroying a target and asserting the chosen policy outcome.
- [ ] Add wildcard relation queries that enumerate all targets an entity relates to under a given relation, with a test listing multiple targets.
- [ ] Add exclusive-relation enforcement so a relation flagged exclusive replaces any existing target rather than adding a second pair, with a test asserting only the latest target remains.
- [ ] Add a deferred-destruction queue so entity destruction requested mid-iteration is recorded and applied at the next sync point, with a test proving iteration over an entity being destroyed remains valid until the sync point.

## Reflection & Type System

- [ ] Extend component reflection to describe nested struct fields recursively so an inspector or serializer can walk composite components, with a test reflecting a struct of structs.
- [ ] Add fixed-array and dynamic-container field descriptors to reflection with element type and count or stride so array members are enumerable, with a round-trip serialization test.
- [ ] Add string-field reflection with an offset and length/accessor strategy so text component fields can be inspected and serialized, with a read/write-by-reflection test.
- [ ] Add enum reflection that maps enum field values to enumerator names and back so inspectors show names and serializers store stable identifiers, with a name/value round-trip test.
- [ ] Add a general runtime type registry that reflects arbitrary non-component types such as assets and save structs using the same field-descriptor model, verified by reflecting a non-component struct.
- [ ] Add per-field attribute metadata such as min, max, step, tooltip, and hidden to reflection descriptors and expose it through the registry so editor tooling drives validation from a single source.

## Serialization, Snapshots & Save

- [ ] Make the chunked snapshot binary codec endian-portable by writing all multi-byte integers in a fixed little-endian layout and byte-swapping on big-endian hosts, verified by decoding an opposite-endian fixture.
- [ ] Add a component-schema version and field-layout table to the snapshot and component binary format plus a migration step that reorders, adds, or drops component fields when the stored layout differs from the runtime layout, verified by loading a fixture whose component gained and lost a field.
- [ ] Add optional block compression to the chunked snapshot codec with a header flag so large snapshots shrink on disk, verified by a compress/decompress round-trip and a size-reduction assertion.
- [ ] Add a whole-file checksum to the snapshot and save-game formats that is written on encode and validated on decode so corrupted files are rejected, with a bit-flip detection test.
- [ ] Extend save-game migrations with a value-transform migration kind that applies a caller-supplied function to convert a value's type or units between versions, with a transform-applied test.
- [ ] Add a component serialize/deserialize hook interface for components whose in-memory form differs from their persisted form, invoked by the snapshot restorer instead of a raw byte copy, verified by round-tripping a component holding an asset handle.

## Asset Management

- [ ] Add an asset dependency graph that records which assets reference which and propagates hot-reload to dependents when a base asset changes, verified by reloading a material and asserting a dependent mesh instance observes the update.
- [ ] Add an asynchronous streaming loader that enqueues load requests with priorities onto the shared job system and resolves handles on completion, so callers request-then-poll without blocking, with a priority-ordering test.
- [ ] Add a budget-driven cache eviction policy that unloads least-recently-used unreferenced assets when a configured memory budget is exceeded, integrated with the memory tracker tags, verified by loading past budget and asserting the coldest asset is evicted.
- [ ] Add content-hash-addressable asset identity that deduplicates identical imported payloads to one cache entry keyed by content hash so two references to identical content share one resident copy, with a dedup-count test.
- [ ] Add a generational-index runtime handle alongside the shared-pointer handle so systems can store a trivially-copyable, revocable reference that reports staleness after reload or unload, with a stale-detection test.

## Coroutines, Async & Script Host

- [ ] Add a coroutine task type whose awaiter suspends onto the shared job system and resumes on a worker when the awaited job completes, giving native systems await-style async without blocking threads.
- [ ] Add a frame-yielding coroutine primitive for wait-next-frame and wait-seconds driven by the scene task loop so gameplay logic can span frames without hand-rolled poll state machines, with a multi-frame resume test.
- [ ] Add real Lua coroutine suspension by routing behaviour calls through new-thread and resume so a script yield suspends and resumes across frames instead of running to completion, verified by a script that yields with preserved locals.
- [ ] Add a persisted execution-position field and a wait node kind to the visual-graph runtime so a graph can suspend at a node and resume there next invocation, verified by a graph that pauses and continues from the same node.
- [ ] Add a structured cancellation token threaded through the async task and streaming-load APIs so an in-flight coroutine or load can be cancelled at its next suspension point and releases its resources, with a cancel-mid-flight test.
