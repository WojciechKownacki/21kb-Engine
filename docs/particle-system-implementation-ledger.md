# 21kb Particle System implementation ledger

## Baseline

- Branch: `1.0`.
- Base commit: `2daecc9f62535eec27f929beb71d64b8a025f21c`.
- Initial dirty state: two pre-existing untracked source documents under `docs/`; both are user-owned, protected, and excluded from implementation scope.
- Initial production-code changes: none.

## Current package

- Stage: `3` — deterministic CPU fixed-step simulation and baseline modules.
- Status: `in_progress`; checkpoint 3.3B1 is accepted.
- Scope: Stage 3 builds deterministic CPU fixed-step simulation, dense runtime state, bounded queues, baseline modules, owner policies, and telemetry on top of the delivered provider ABI.
- Next gate: collision and sub-emitter execution with bounded ordered events, followed by owner policies and reload behavior.

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

## Accepted decisions and mappings

- Plugin identifier: `Rendering.21kbParticle`.
- Product display name: `21kb Particle System`.
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
- Next gate: stage 3.3B2 adds CollisionPlane and SubEmitter execution with bounded ordered events; owner policies and reload remain separate Stage 3 checkpoints.
