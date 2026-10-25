# ECS runtime contracts

This page states what the native ECS guarantees and what it does not, and names the test that
backs each statement. Where a statement has no test, it says so.

## Entity handles and column pointers

* An `Entity` id carries a generation in its upper 32 bits. When a destroyed entity's slot is
  reused, the new entity gets a different id. A handle of a destroyed entity is never alive again.
* `IsAlive` is false for a stale handle. `TryGetComponentData` and `TryGetMutableComponentData`
  return `nullptr` for a stale handle or a missing column. `ComponentData` throws for a stale handle.
* A pointer returned for a component is valid until the next structural change in the same
  storage: creating or destroying any entity, adding or removing a component (single or bulk),
  `Clear`, or a chunk pool trim. Destroying an entity moves the last row of its chunk into the
  hole, and adding or removing a component moves the entity to another archetype table. Fetch the
  pointer again after any of these. Writing through a pointer that was fetched before such a change
  is a bug even when it appears to work.
* Tests: `RunSwapDeleteAndGenerationTest` (one slot), `RunLongChurnWithStaleHandlesTest` (30000
  random create, destroy, add, remove and set steps against a reference model; checks that every
  destroyed handle stays dead, that slots were really reused, and that data survives compaction and
  migration), both in `EcsNativeArchetypeStorageTests.cpp`.
* Not covered: a memory sanitizer run. The test checks behaviour, not addresses.

## Command buffer playback order

* `CommandBuffer` has one lane per worker. Playback replays the lanes in lane index order, and each
  lane in the order its commands were recorded.
* The order in which different workers recorded their commands does not matter, and neither does
  the number of lanes: of two conflicting writes to one component, the one in the higher lane wins.
* A failed playback is rolled back (`RunCommandBufferRollbackOnPlaybackErrorTest`).
* Tests: `RunCommandBufferLaneOrderResolvesConflictsTest`,
  `RunCommandBufferMultiWorkerDeterministicStructuralChangesTest`,
  `RunCommandBufferBulkPlaybackOrderGuaranteeTest`, `RunCommandBufferRandomStructuralChangeStressTest`
  in `EcsCommandBufferTests.cpp`.

## Determinism scope

What is promised: with `SystemSchedulingMode::Deterministic`, the `Deterministic` query policy,
iteration order and reduction mode, the same registration order and the same fixed step, the
scheduler runs the systems in the same order and the components end with the same values, whatever
the number of workers. Independent systems are ordered by name.

What is not promised:

* bitwise identical floating point results on different platforms or compilers. The golden checksum
  in `EcsDeterministicReplayTests.cpp` is an exact value for MSVC on x64 only. Linux and ARM runs of
  the same test are evidence about those platforms and are still to be recorded; a differing result
  must be reported, not re-baselined;
* determinism of the physics backend, the GPU, scripts that use time or random sources, or anything
  that depends on thread timing outside the deterministic scheduler;
* determinism of the non-deterministic scheduling modes.

Tests: `RunDeterministicReplayProducesStableResultTest`,
`RunDeterministicReplayIgnoresWorkerCountTest` (0, 1, 2, 4 and 7 workers) and
`RunDeterministicReplayGoldenTest`.

## Declared access validation

`RuntimeAccessValidator` compares the read and write sets that systems and jobs declare. Two jobs
that are active at the same time and declare a read/write or write/write overlap on a component are
rejected with a `std::logic_error` that names both jobs and the component. It does not observe what
the code really reads or writes, so it is not a data race detector. A job that touches a component
it did not declare is not caught. No thread sanitizer is available for the MSVC toolchain used here.
Tests: `RunRuntimeAccessValidator*` in `EcsSystemSchedulerTests.cpp`, `kb_ecs_stress_tests`,
`kb_ecs_scheduler_correctness_tests`.

## Native storage memory budget

`WorldConfig::maxNativeStorageCommittedPayloadBytes` (0 means no limit) caps the committed chunk
payload: the number of chunks the pool has allocated multiplied by the chunk payload size. Free
chunks that are reused do not count again. When a create or a component migration would need a new
chunk beyond the cap, the call throws `std::length_error` and the world is unchanged.

What the cap does not cover: chunk metadata, entity records, side payload (sparse, shared and
external components), the mirrored Flecs world, and any memory outside the ECS. Telemetry reports
these separately (`chunk_metadata_bytes`, `entity_record_bytes`, `side_payload_bytes`,
`tracked_bytes`, `committed_bytes`), so they can be told apart from the capped payload.

Tests: `RunChunkCommitGuardTest` (exhaustion, no leaked entity on a rejected create, reuse of free
chunks, rejected migration) and `kb_ecs_native_allocation_fault_tests` (the `fault-recovery 128` run injects
allocation failures and checks that the world stays consistent).

## Telemetry and estimates

`WorldTelemetrySnapshotToJson` (schema `kb.ecs.world_telemetry.v1`) ends with a `metric_basis`
object that says how to read the numbers:

* counters and `*_nanoseconds` fields are measured while the world runs (`steady_clock`);
* `query_bytes_touched`, `query_average_bytes_per_entity` and the four
  `*_estimated_*_per_second` fields are estimates: component sizes multiplied by the entities
  visited, divided by the elapsed time. They are not DRAM bandwidth;
* `*_percent` and the other `query_average_*` fields are derived from the measured counters;
* `hardware_counters` and `dram_bandwidth` are `"unavailable"`: nothing reads performance
  monitoring counters yet.

The export has a fixed size, so a long session does not grow it. Units follow the key suffix.
There is no `kb_cli` command that prints it yet. Test: `EcsInspectionTests.cpp` (`metric_basis`).

## ProfilerCounters

`kb::core::ProfilerCounters` is a debug helper. It is on in debug builds and off in `NDEBUG`
builds unless constructed with `ProfilerCounters{ true }`. A region from `BeginScope` is recorded
when it ends, with its start, duration and nesting depth, also when the scope is left by an
exception. Storage is bounded: at most 4096 timeline events and 256 counter names are kept, and
what does not fit is counted in `DroppedTimelineEvents()` and `DroppedCounters()`. Test:
`EngineLibraryTests.cpp` (profiler block).
