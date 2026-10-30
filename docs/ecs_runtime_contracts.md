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

`SceneRuntime::SetDeterministicExecution(true)` opts into dependency/name ordered ECS
execution and phase/registration ordered scene execution on the owner thread. It also
removes the fixed-step early exit based on measured CPU duration. Disabling restores
automatic ECS scheduling. `DeterminismScope()` returns the active policy and the
simulation, RNG, fixed-tick and backend contracts used by the CLI report.

The same binary, registration, initial state and ordered inputs produce the same
results only when content also uses deterministic query iteration/reductions and
controls its external inputs. Internal jobs or asynchronous work started by a system
remain that system's responsibility. Deterministic ECS dispatch alone does not alter
user query policies.

Enabled mode rejects zero, negative, NaN and infinite frame deltas before changing
scene state; zero would otherwise ask the backend to measure wall time. Fixed ticks
depend on the same finite positive frame delta sequence, playing state and
`FixedStepSettings()`. Frame deltas are clamped, at most `maxFixedStepsPerFrame`
steps run, and excess whole-step debt is discarded. CPU duration does not affect
this decision in enabled mode. Variable Tick still receives the supplied frame
delta. Equivalent elapsed time with different frame partitions is not a guarantee.

`kb::math::NextUInt32` has an exact uint32 sequence for the same seed/counter;
the complete state is the caller-owned seed and counter, including modulo counter
wrap. Preserve stream ownership, consumption order and state on restore. Floating
distributions are subject to floating-point rounding; Lua `math.random`, OS entropy,
GPU random sources and wall-clock seeding are outside this contract.

`kb_cli run --project <dir> --deterministic --report determinism` executes the actual
headless scene and emits `21kb.determinism/v1` with its policy, fixed-step settings,
completed frame/tick counts and scope. The CLI also uses synchronous physics stepping.
Without `--deterministic`, the report explicitly says the policy is disabled.
JSON goes to stdout and runtime logs to stderr.

No general cross-platform bitwise floating-point or backend guarantee is made. Physics
requires the same binary, ordered mutations and synchronous steps; cross-platform
physics comparisons need scenario-specific tolerances. GPU execution, external I/O,
asynchronous asset readiness and scripts using wall time are outside this guarantee.
The API and CLI do not invent a universal backend tolerance: the report uses null and
states that the comparison scenario must supply its bounds.

`kb_ecs_deterministic_replay_tests` checks the ECS recurrence against a double-precision
numeric golden with absolute component tolerance `0.0001` on every platform. It also
retains the exact MSVC x64 quantized checksum `536938034` and prints observed checksums
on other platforms. Worker counts 0, 1, 2, 4 and 7 must agree within a run.
The scene replay checks exact RNG golden `13224159966847899506` (1024 draws), counter
wrap and state restoration, 12 bounded fixed ticks over fractional/clamped frame
deltas, serial scene order with 1/2/4 workers, and steps deliberately slower than their
simulation delta. These integer/tick comparisons have zero tolerance. The test prints
portable results for comparison across hosts; Linux and ARM execution evidence is
still required before claiming qualification on those hosts. The CLI/Lua workflow
also checks synchronous Jolt constant-velocity motion against its numeric golden
with absolute position/velocity tolerance `0.001`; this is a bound for that simple
scenario only. Engine module ABI 24 requires providers to be rebuilt with the
scene execution policy.

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
`kb_cli run --project <dir> --report ecs` runs the scene for `--frames` frames (default 60) and prints it,
read only, as one JSON object on stdout (`schema` `21kb.ecs-telemetry/v1`, `succeeded`, the frame counts and
the snapshot under `telemetry`); logs stay on stderr. The number of ECS worker threads is not a
setting of the scene yet, so there is no CLI option for it.
Tests: `EcsInspectionTests.cpp` (`metric_basis`) and `RunParticleReportCommandTests` in `KbCliTests.cpp` (the ecs report).

## Scheduler lifecycle and planning

`SystemScheduler::Remove(system, world)` detaches an owned system by pointer
between updates and invalidates its dependency plan and trace. It calls
`OnDestroy` once; even if that callback throws, the system stays detached.
Removing an explicit ordering target makes the next rebuild report an unknown
dependency before any systems run. Removing the dependent or adding a replacement
repairs the graph. Add, Remove, Update and Shutdown reject overlapping or
reentrant calls, including attempts to remove a system from its own callback.
Other scheduler access remains confined to its owner thread.

`PlanningStats()` measures Update's cached execution-plan rebuilds separately
from execution timing; `ExecutionOrderSnapshot()` is an unmeasured inspection. It reports
attempts, successful rebuilds, last wall duration, system/edge/stage counts and
whether the last attempt succeeded. Frames reusing the cached graph leave these
values unchanged; Shutdown resets them. The scheduler suite measures a 128-reader
graph, adding a conflicting writer and removing it, and checks cache reuse,
failed-dependency recovery and exactly-once removal cleanup.

## ProfilerCounters

`kb::core::ProfilerCounters` is a debug helper. It is on in debug builds and off in `NDEBUG`
builds unless constructed with `ProfilerCounters{ true }`. A region from `BeginScope` is recorded
when it ends, with its start, duration and nesting depth, also when the scope is left by an
exception. Storage is bounded: at most 4096 timeline events and 256 counter names are kept, and
what does not fit is counted in `DroppedTimelineEvents()` and `DroppedCounters()`. Test:
`EngineLibraryTests.cpp` (profiler block).

## Parallel scene systems

Scene systems normally run one after another on the thread that calls `SceneRuntime::Update`. A system
can opt in to running at the same time as others by implementing `IParallelSceneSystem`
(`engine/scene/ParallelSceneSystem.hpp`) next to `SceneSystem` and declaring the resource names its
`OnUpdate` and `OnFixedUpdate` read and write.

* Systems still run in registration order. The scheduler runs a group of consecutive systems together
  only when every one of them opted in and no two conflict (one writes a name the other reads or
  writes). A system that did not opt in is a barrier: everything before it finishes first and
  everything after it starts later. A conflicting system ends the group and starts the next one.
* The result therefore equals the serial result as long as the declared access is true. The scheduler
  cannot check that; a system that touches something it did not declare breaks the guarantee. A system
  that creates or destroys entities, adds or removes components or reparents declares
  `kSceneStructureResource` as a write.
* Errors from a group are recorded after it has finished, in registration order. The error lists are
  guarded by a mutex because a system may call `SceneSystemContext::ReportError` from a worker.
* Each scheduler that has a parallel system owns a worker pool (created at the first group of two or
  more) with the default worker count; with fewer than two workers the group runs serially.
  A scheduler without parallel systems takes the same serial loop as before.
* No built-in system opts in yet, after an audit (below). The animator, transform and navigation
  systems also already split their own work across worker pools. A measured example: four independent systems of about 3.6 ms each
  took 14.4 ms in a frame serially and 3.7 ms in parallel on a 24 thread machine
  (`kb_engine_tests scene-system-parallel-bench`).

Tests: `SceneSystemParallelTests.cpp` (`kb_engine_tests scene-system-parallel`): identical results
serial and parallel, systems really overlapping, conflicts and barriers keeping order, errors in
registration order, removal and shutdown.

### Audit of the built-in scene systems

Each system was read for what its update touches, not for what it ought to touch. A system can opt in
only if everything it touches is either private to it or declared, and is safe on a worker thread. None
passes today, so all stay barriers; the scheduler runs them exactly as before.

| System | Phase | What the code does | Verdict |
| --- | --- | --- | --- |
| Audio (`MiniaudioSceneSystem`) | update | `AudioPlayback` asserts the scene's owner thread in every entry point (`AudioPlayback.cpp`) | cannot run on a worker |
| Navigation | update | moves agents with `scene.Transforms().Set` (`SceneNavigationRuntime.cpp:141`) and `PhysicsBackend::CharacterMove` (`:290`) | writes shared transforms and the physics backend |
| Content instances | update | `scene.Prefabs().Instantiate` and `scene.Entities().Destroy` (`SceneContentInstanceService.cpp:99,111`) | changes entity structure |
| AI (`AIBrainSystem`) | update | `Transforms().Set`, `NavAgents().MarkModified`, `PhysicsBackend::CharacterMove`, `Animators().SetFloat`, reads combatants and the gameplay service (`AIBrainSystem.cpp`) | writes transforms, navigation, physics and animators |
| Gameplay | update | marks `Combatants` modified and reads every transform (`SceneGameplayService.cpp:46,57,306`) | reads transforms that animation and navigation write |
| Animator | update | 3740 lines of `SceneAnimatorService.cpp`: advances poses, applies socket attachments, publishes debug snapshots; transforms are its output | not shown to be independent of any transform writer |
| Timeline, UI, particles, 2D map, scripts, input, physics | update / fixed | read or write transforms, entities, the UI frame or Lua state through scene-wide services | not audited line by line; barriers |

What would make parallel scene systems pay off on a real scene is not more declarations but a safe way
for a system to write: a system that only reads the scene during its update and returns its changes
as commands, played back in registration order after the group (the ECS `CommandBuffer` already has
lanes and a defined playback order, see above). AI thinking and the gameplay senses are read heavy and
would be the first candidates; they need splitting into a read part and a write part first.

## Dependency graph execution

`SystemSchedulerParallelismMode::SystemsAndChunks` is an opt-in execution mode
for ECS systems. Dependencies release graph successors as they complete; sync
points execute on the scheduler's owner thread. A system can synchronously submit
jobs, batches or chunks to its execution pool, whose workers help while waiting.
Nested graph dispatch and asynchronous submissions to that same pool are rejected.
Graph dispatch from a regular worker callback on the same pool is also rejected.
Configure the scene scheduler between updates using `SceneRuntime::SetEcsParallelismMode`.
For headless runs, `kb_cli run --ecs-parallelism combined` selects the same mode;
`systems` and `chunks` select the existing alternatives. Without the option the
scene's configured mode is preserved.

The `job_graph` trace mode identifies a graph that actually ran. In this mode the
worker-pool dispatch counters describe the worker pump dispatch, not the number of
graph nodes or chunk jobs. Pump active time includes time spent waiting for work;
system event durations include time spent waiting for nested work and can include
helping another system's chunks. These durations are elapsed time, not measured
thread CPU time or a reliable measure of worker utilization.

## Chunk delta application

`World::ApplyChunkedDeltaSnapshot` prepares and validates the complete input before
deleting entities, writing columns or changing archetypes. Invalid payload sizes,
duplicate identities or component columns, stale live generations, overlapping
deletion/update sets and inconsistent final entity counts return `false` without
applying an earlier part of the delta. A destroyed identity may be replaced by a
new generation at the same index when that deletion is included in the delta.
Structural changes remain forbidden during query iteration.

This rejection guarantee covers malformed input. Allocation failures and user
observer exceptions during application are not a rollback transaction. Chunk
snapshots contain native component columns; they do not capture physics solver
state, Lua execution state, entity names or backend relations. They must not be
used alone as a complete runtime continuation snapshot.
