# 21kb Particle System implementation ledger

## Baseline

- Branch: `1.0`.
- Base commit: `2daecc9f62535eec27f929beb71d64b8a025f21c`.
- Initial dirty state: two pre-existing untracked source documents under `docs/`; both are user-owned, protected, and excluded from implementation scope.
- Initial production-code changes: none.

## Current package

- Stage: `4` — core-owned immutable render snapshot.
- Status: `accepted`; Stages 3 and 4 are accepted.
- Scope: Stage 4 adds a renderer-neutral packed stream and bounded retained snapshot channel with revision, backend epoch, fixed-step index, tombstone, and no DLL-owned destruction path.
- Next gate: Stage 5 GPU particle renderer consumes retained snapshots and replaces proxy-per-particle rendering.
- Rendering direction: normal game-facing simulation and rendering must move to GPU. The CPU backend is retained only as the deterministic validation, diagnostics, and preview path; it is not the intended final gameplay path.

## Accepted stages

### Stage 0 — characterization: `accepted`

- One test-owned flat `.kbvfx` fixture is shared by engine characterization and headless automation.
- Every legacy scalar, vector, curve key, and gradient stop has a reviewable future semantic mapping.
- Existing CPU behavior is frozen by hash `14160509741928470306` for the named Debug configuration.
- A 2,048-sample deterministic test distinguishes the current linear-angle cone distribution from uniform solid-angle sampling.
- Two 64-bit seeds sharing the lower 32 bits produce the same state hash, characterizing the current truncation.
- Public asset invalidation proves the current successful no-op `Emit` result with unchanged live count.
- Existing engine/renderer tests retain public and script API, completion/owner lifecycle, one-mesh-proxy-per-live-particle, and cleanup coverage.
- Production-code diff for this stage: empty.

### Stage 1A — v2 schema, canonical IO, migration, and structural validation: `accepted`

- `ParticleEffectAsset` stores only the v2 authored model; the legacy representation exists only during parse-to-migration, and runtime compatibility is a derived view.
- Canonical v2 IO is strict UTF-8, byte-stable, LF-only, bounded to 512 KiB, and reports line, property path, emitter ID, and module ID where applicable.
- The non-owning record parser has a schema-derived 28,640-record ceiling; legal documents above 4,096 records pass, while hostile record/count inputs fail before unbounded allocation.
- Structural validation covers typed modules and outputs, stable IDs, ranges, ordering, output reference slots, aggregate CPU capacity, and internal sub-emitter/event cycles and depth.
- Legacy sources migrate in memory without rewriting their source; atomic-save failure preserves an existing regular destination and the prepared temporary conflict byte-for-byte.

### Stage 1B — typed dependencies and recipe content: `accepted`

- `ParticleEffectAssetValidator` owns dependency analysis; both asset-loader hooks delegate to the same result.
- Discovery publishes sorted, deduplicated direct edges, while validation walks the bounded transitive graph and rejects missing, mismatched, wrong-type, direct, indirect, and self-cyclic references with precise source context.
- Material and material-instance, mesh, texture-atlas, and external-effect references preserve ID and virtual path semantics.
- The dedicated dependency-graph limit accepts 256 nodes and rejects node 257.
- Exactly 15 canonical v2 recipe assets load and pass structural and dependency validation with the expected display names and established browser categories.

### Stage 2 — scene component, provider ABI, and plugin lifecycle: `accepted`

- `ParticleEffectComponent` persists all nine fields through scene v33, prefab capture/bulk instantiate, compare/hash, apply/revert, variants, reflection, scripting, and presence overrides; v32 scenes remain loadable.
- `ParticlePlayback` owns the typed engine ABI, bounded event queue, live-state queries, backend registration ownership, and explicit `BackendUnavailable` behavior without a production simulation fallback.
- `Rendering.21kbParticle` loads `PreDefault`; real DLL attach/detach/reload is clean for script and no-script scenes across 100 cycles, including host-owned script-system cleanup.
- New projects enable the provider; existing projects receive an explicit Plugins-panel Add/Cancel migration, while CLI and standalone reject `.kbvfx` projects lacking an enabled provider without silently rewriting descriptors.
- The old engine-owned particle simulator was removed from `ScriptRuntimeSceneSystem`; particle completion events use the core-owned bounded queue and drain post-fixed before `Tick`.
- The plugin DLL is staged for editor and standalone targets; the editor headless boundary test verifies the exact unavailable-backend diagnostic until Stage 3 supplies the real backend.

### Stage 3.1 â€” CPU backend lifecycle and preallocated storage: `accepted`

- Plugin-owned `CpuParticleBackend` registers only while its `ParticleSceneSystem` is attached and unregisters only its own backend during detach/reload.
- Instances use generation-safe IDs over a dense 256-instance store; stale handles cannot address a reused slot.
- Particle SoA capacity, command/event buffers, and per-instance scalar overrides derive from shared schema ceilings and are reserved by explicit `Warmup()`.
- Real lifecycle, validation, query, and bounded-copy behavior are present before simulation; non-zero `Emit` explicitly returns `UnsupportedOutput` until the fixed-step kernel exists.
- Focused tests prove limits, stale handles, backend ownership/reload, caller-span bounds, and zero global allocations after warmup for the lifecycle/query path.

### Stage 3.2 â€” deterministic fixed-step CPU kernel: `accepted`

- `ParticleSceneSystem` participates in the engine-owned post-simulation fixed scheduler at exactly 1/60 seconds with the shared eight-step catch-up ceiling; no second accumulator exists.
- The CPU backend compiles and generation-caches validated v2 assets into bounded runtime data, then simulates continuous and burst spawning, lifetime expiry, duration, looping, draining, and per-emitter authored prewarm through the same fixed-step kernel.
- Per-instance state owns its time, emission remainders, burst cursors, random stream, and live-particle counts; particle storage remains bounded SoA with no heap allocation in a fixed step after warmup.
- Explicit schema and runtime limits bound prewarm, continuous spawn rate, per-step spawn budget, per-emitter capacity, and the 262,144-particle scene capacity; telemetry reports admitted and rejected spawn demand.
- Focused tests cover 30/60/144 Hz fixed-step parity, catch-up, seeded instance independence, one- and two-emitter prewarm, burst/lifetime/duration/loop/drain behavior, capacity boundaries, and allocation-free stepping.

### Stage 3.3A â€” CPU initial velocity and force modules: `accepted checkpoint`

- The compiler accepts and executes only `InitialVelocity`, `Gravity`, `Wind`, and `Drag` from the baseline module set; other module variants remain an explicit unsupported result until their executors arrive.
- Initial velocity uses deterministic uniform solid-angle cone sampling; force execution preserves authored enabled-module order and runs in the shared spawn, age/death, force, and integration step used by prewarm.
- Gravity has one engine-owned default scene-gravity constant and mutually exclusive custom-acceleration or scene-scale channels. Legacy migration now maps its historical gravity scale exactly to the scene-scale channel.
- The focused CPU backend suite independently passed after build: solid-angle statistics plus golden `10334376869005698480`, force ordering/enables, invalid payload rejection, legacy runtime mapping, 30/60/144 Hz module parity, prewarm parity, and allocation-free module stepping.

### Stage 3.3B1 â€” CPU visual-over-life modules and module ABI guard: `accepted checkpoint`

- `ColorOverLife`, `SizeOverLife`, and `AlphaOverLife` compile into fixed-size engine-schema arrays and execute in the shared fixed-step/prewarm kernel. Their output uses preallocated SoA channels and is copied into the core-owned runtime state as color and size; final alpha is gradient alpha multiplied by the alpha curve.
- The state layout change is protected by the actually enforced engine-module ABI version. A deliberately old test DLL is rejected by the production host before its `create` entry point executes; the current `Rendering.21kbParticle` DLL still completes the 100-reload lifecycle test.
- Focused tests cover exact curve/gradient values and golden `3957020943848305260`, disabled defaults, malformed payload rejection, visual 30/60/144 Hz and prewarm parity, allocation-free stepping, and explicit unsupported results for the remaining CollisionPlane and SubEmitter variants.

### Stage 3.3B2 — CPU collision and ordered internal events: `accepted checkpoint`

- `CollisionPlane` corrects penetration, applies normal restitution and tangential friction, then produces bounded collision events. `SubEmitter` and internal target-emitter bindings execute breadth-first in authored order for birth, death, and collision triggers.
- The event queue has the schema hard ceiling; queue overflow and per-action exhaustion return distinct typed outcomes for explicit commands. Automatic fixed simulation remains non-fatal and reports exact event, spawn-budget, and capacity telemetry.
- Structural validation always rejects emitter cycles, while acyclic paths respect each action's local depth limit. A five-emitter birth graph with depth three consequently emits exactly four particles.
- Failed `Play` or `Restart` prewarm atomically rolls back to a stopped, empty instance. Runtime state carries bounded event depth and prewarm group channels without fixed-step heap allocation.
- Focused tests cover collision enable/disable physics, ordering, all triggers, queue and action boundaries, depth, script diagnostics, automatic-limit telemetry, prewarm rollback, and allocation-free event handling.

### Stage 3.3C — component ownership, reload, and runtime overrides: `accepted checkpoint`

- Enabled active components own one runtime instance; activation, disable/removal, `Clear`/`Drain` owner death, and exactly-once finished events have explicit lifecycle behavior. Destroyed-owner bindings free their fixed reconciliation slots without cancelling backend-owned draining.
- Asset generation refresh preserves compatible state, deterministically restarts incompatible topology, retains the last known good runtime for invalid candidates, and exposes query-only `Restarted` and `StaleAfterInvalidReload` states.
- Component rate multiplier, total instance capacity override, and transform-follow policy are applied on the fixed boundary. Local-space particles follow full owner transforms; world-space particles and frozen transforms do not move retroactively.
- Internal event actions are deferred to the next fixed step with bounded FIFO queues, authored module-before-binding order, and one level of expansion per step. Raw instances keep explicit identity/default component behavior.
- Missing or non-executable component effects now fail explicitly, and a failed asset replacement leaves the previous live instance intact.
- Focused tests cover 256-instance atomic turnover, reload/LKG, lifecycle, event timing/depth, transform and override semantics, and allocation-free warmed paths.

### Stage 4A — immutable render snapshot core: `accepted checkpoint`

- `kb_engine` owns the renderer-neutral 64-byte packed particle stream and the complete per-emitter batch metadata; no renderer or provider type is part of either DTO.
- A scene-owned channel preallocates four immutable 16 MiB retained slots. Its header carries monotonic revision, scene ID, backend epoch, fixed-step index, and tombstone state.
- Publication validates batch ranges, capacity, enums, finite bounds, compact particle payload, and drop diagnostics before replacing the retained revision. Exhaustion is explicit `SnapshotBackpressure` and leaves the last complete snapshot intact.
- The channel owns all control blocks and storage in `kb_engine`, so a retained reader remains valid after provider unload and scene destruction. Focused tests prove concurrent readers, no-allocation publish after warmup, metadata retention, and lifecycle safety.

### Stage 4B — CPU-preview snapshot publication and tombstone: `accepted checkpoint`

- The CPU preview backend captures previous positions, stable particle IDs, compiled output metadata, bounds, per-emitter ranges, and overflow telemetry into preallocated scratch, then publishes exactly one immutable snapshot after each fixed step.
- Each publication is stamped with a monotonically increasing revision, the completed scene fixed-step index, and the current backend epoch. Typed snapshot backpressure remains observable while preview simulation continues and the last complete revision is retained.
- Terminal snapshots use small engine-owned header slots, so teardown can publish a tombstone even when all four 16 MiB payload slots are retained. If terminal retention itself is exhausted, detach fails explicitly before backend ownership changes and can be retried after a reader releases a snapshot.
- Focused tests prove two independent viewport reads see one immutable fixed-step revision, stable previous/current particle data and two-emitter ranges, payload and terminal backpressure, scene/provider lifetime, and allocation-free warm paths.

## Accepted decisions and mappings

- Plugin identifier: `Rendering.21kbParticle`.
- Product display name: `21kb Particle System`.
- Production rendering policy: GPU simulation and GPU rendering are mandatory for the normal runtime path; CPU execution is limited to explicit preview, diagnostics, and deterministic verification duties.
- Stable asset/type terminology: `ParticleEffect` and `.kbvfx`.
- Existing flat asset model maps to `sources/engine/include/engine/scene/ParticleEffectAsset.hpp`.
- Existing parser/writer maps to `sources/engine/src/scene/ParticleEffectAssetIO.cpp`.
- The pre-Stage-2 CPU simulation mapped to `sources/engine/src/scene/SceneParticleSystemService.cpp`; that legacy owner was removed and is retained only in repository history.
- Existing render bridge maps to `sources/renderer/src/scene/SceneParticleRenderSynchronizer.cpp`.
- Existing script and lifecycle characterization resides in `sources/engine/tests/ScriptRuntimeTests.cpp`; renderer characterization resides in `sources/renderer/tests/RenderSceneSyncTests.cpp`.

## Files changed for 21kb Particle System

- `docs/particle-system-implementation-ledger.md` — orchestration state only.
- `sources/editor/tests/HeadlessAutomationScenario.json` — consumes the shared legacy fixture.
- `sources/editor/tests/fixtures/LegacyAutomationParticleEffect.kbvfx` — test-owned legacy source fixture.
- `sources/engine/tests/ScriptRuntimeTests.cpp` — stage 0 characterization coverage.

## Validation evidence

- `git branch --show-current` — exit `0`, result `1.0`.
- `git rev-parse HEAD` — exit `0`, result `2daecc9f62535eec27f929beb71d64b8a025f21c`.
- `git status --short --untracked-files=all` — exit `0`, exactly two pre-existing untracked source documents under `docs/`; no production-code changes.
- `cmake --build build --config Debug --target kb_engine_tests` — exit `0`.
- `ctest --test-dir build -C Debug -R "^kb_engine_tests$" --output-on-failure` — exit `0`, `1/1`, `149.91 s`.
- `cmake --build build --config Debug --target kb_renderer_tests` — exit `0`.
- `ctest --test-dir build -C Debug -R "^kb_renderer_tests$" --output-on-failure` — exit `0`, `1/1`, `325.34 s`.
- `cmake --build build --config Debug --target kb_editor` — first wrapper timed out while its verified compiler child remained active; the child completed, and the immediate incremental rerun exited `0`.
- `ctest --test-dir build -C Debug -R "^kb_editor_headless_automation_scenario$" --output-on-failure` — exit `0`, `1/1`, `90.61 s`.
- `git diff --check` — exit `0`.
- Added-line and new-artifact terminology/incomplete-implementation scan — clean.
- `cmake --build build --config Debug --target kb_21kb_particle_asset_tests kb_engine_tests kb_renderer_tests --parallel 4` — independent exit `0` after the stage 1A repairs.
- `build/engine/Debug/kb_21kb_particle_asset_tests.exe` — independent exit `0`.
- `build/engine/Debug/kb_engine_tests.exe script` — independent exit `0`.
- `build/renderer/Debug/kb_renderer_tests.exe scene-sync` — independent exit `0`.
- Final stage 1A headless CTest — exit `0`, `1/1`, `93.61 s`.
- Final stage 1B focused CTest — independent exit `0`, `1/1`, `1.36 s`.
- Final stage 1B `kb_engine_tests.exe script` — independent exit `0`.
- Final stage 1B headless CTest — exit `0`, `1/1`, `83.11 s`.

## Rejected attempts, open defects, and next gate

- The first controlled CPU-golden run intentionally used an impossible expected hash and failed with actual `14160509741928470306`; the exact observed value is now frozen and passes.
- The current cone distribution, upper-seed truncation, and successful no-op emit are characterized defects scheduled for runtime replacement, not accepted target behavior.
- Stage 1A attempt 1 was rejected after independent review despite a green focused test; every listed defect was repaired and the second attempt was accepted.
- No open stage 0 defect remains.
- No open stage 1 defect remains.
- Stages 3 and 4 are accepted. Next gate: Stage 5 GPU particle renderer; CPU remains a non-gameplay verification and preview path.
