# 21kb Particle System implementation ledger

## Baseline

- Branch: `1.0`.
- Base commit: `2daecc9f62535eec27f929beb71d64b8a025f21c`.
- Initial dirty state: two pre-existing untracked source documents under `docs/`; both are user-owned, protected, and excluded from implementation scope.
- Initial production-code changes: none.

## Current package

- Stage: `9` — trail, ribbon, and beam output.
- Status: `accepted`; Stages 0 through 9 are accepted.
- Scope: Stage 9 adds bounded trail history, ordered ribbon breaks, fixed-step beam geometry, and renderer-owned dynamic strip buffers per `docs/particle-kanku-audit-plan.md` section 11 ("Etap 9 — Trail, Ribbon i Beam").
- Canonical spec source: `docs/particle-kanku-audit-plan.md`. Its own naming (`Particle`/`kb_particle_core`/etc., after the `Particli`→`Particle` document-only rename on 2026-08-15) is not literal — this project's frozen naming (`Rendering.21kbParticle`, `kb_21kb_particle_*` targets, `ParticleEffect`/`Particle*` class names without the `21kb` prefix where the existing code already uses that shorter form) stays as-is. Where the audit's file/class names differ from what actually exists in this repo, map to the real equivalent and record it here rather than renaming existing code to match the audit.
- Orchestration: single-agent (no 2-agent orchestrator/implementer split — `docs/archive/particle-sol-xhigh-autonomous-prompt.md`'s mechanics are archived/unused, per explicit user decision 2026-08-15).
- Next gate: Stage 10 gate criteria (audit section 11, "Etap 10"); Stage 9 is closed only by the evidence recorded below.
- Rendering direction unchanged: normal game-facing simulation and rendering must move to GPU. The CPU backend is retained only as the deterministic validation, diagnostics, and preview path; it is not the intended final gameplay path.

### Stage 8 package 1 of 2 — data plumbing and capability gate: closed, independently verified

Schema (`ParticleMeshOutput.lodBias/castsShadow/receivesShadow`), structural validation, and dependency
validation for the mesh reference already existed before this stage (confirmed by reading the code, not
assumed). This package makes `ParticleOutputType::Mesh` a real, compilable, cacheable, simulatable
capability end-to-end through every layer *except* the renderer actually drawing anything for it yet:

- `ParticleCompiledEmitter` (`sources/engine/include/engine/particles/ParticleCompiledEffect.hpp`) gained
  `meshLodBias`/`meshCastsShadow`/`meshReceivesShadow`.
- `ParticleEffectCompiler.cpp`/`.hpp` (`sources/plugins/21kb_particle`): `ParticleCompilerCapabilities` gained
  `mesh = true`; `Supports()`/`StableKey()` updated; a **latent bug found and fixed**: the output-payload
  copy block's `if (Billboard) ... else if (StretchedBillboard) ... else` unconditionally treated the
  `else` branch as `PointSprite` and called `std::get<ParticlePointSpriteOutput>` — reaching this with
  `Mesh` (a variant holding `ParticleMeshOutput`) would have thrown `std::bad_variant_access` the moment
  the capability gate opened. Replaced with explicit `else if (PointSprite)` / `else if (Mesh)` branches.
- `ParticleRenderEmitterRecord` (`sources/engine/include/engine/particles/ParticleRenderSnapshot.hpp`)
  gained a compact `std::int8_t meshLodLevel` (the schema's float `lodBias` rounded once at compile time
  to match `SceneRenderMeshInstance::lodBias`'s integer-level semantics — carrying a float through the
  tightly budgeted, `<=144U`-byte-per-record retained snapshot wasn't worth it) and two new
  `ParticleRenderEmitterFlag` bits, `CastsShadow`/`ReceivesShadow` (zero extra bytes, same idiom as the
  existing `SoftParticles`/`AntiAliasing` bits).
- `CpuParticleBackend.cpp` (`sources/plugins/21kb_particle`) populates the new snapshot fields, gated to
  `outputType == Mesh` specifically (not unconditionally — see the regression below).
- `ParticleRenderBatcher.cpp` (`sources/renderer`): Mesh-output emitters are explicitly excluded from the
  quad/billboard batch loop (`continue` before the `SupportedOutput()` check) — they are handled by a
  separate mesh-instancing path (package 2, not yet built) that reuses the existing scene mesh pipeline,
  not this 80-byte `ParticleGpuInstance` quad format. Excluding them must not count them as
  dropped/unsupported, since they are not actually unsupported once package 2 lands.
- Three independent regressions were found by rerunning the focused suite after each change (not assumed
  clean from compilation alone) and fixed, each with new/updated test coverage:
  1. **`ParticleRenderSnapshot.cpp`'s `ValidateEmitterRecords`** hard-allowlists which `.flags` bits are
     legal and rejects any other bit — it did not know about the two new bits, so *any* emitter
     record (any output type, since `meshReceivesShadow` defaults `true`) failed validation and every
     snapshot publish silently failed. Fixed the allowlist; added a negative test
     (unknown flag bit rejected) to `ParticleRenderSnapshotTests.cpp`.
  2. **`sources/renderer/tests/ParticleRendererTests.cpp`**'s mixed-effect drop-contract test used `Mesh`
     purely as a stand-in for "any unsupported output type" (true before this stage). Swapped the
     exemplar to `Trail` (still genuinely unsupported) and added a dedicated new test asserting Mesh is
     silently excluded from batch/drop/unsupported counts rather than reported as dropped.
  3. **`ParticleCompiledEffectCache.cpp`** (`sources/engine/src/particles`) — the compiled-effect cache's
     own hand-rolled field-wise binary reader independently capped `outputType` at `PointSprite` and its
     `ValidEffect()` integrity check unconditionally rejected any non-zero `meshAssetId`. Both are gates
     entirely separate from the compiler's capability check and from the schema/validation layers; opening
     the compiler capability alone was not sufficient. Fixed: writer/reader now round-trip the three new
     fields, the output ceiling is `Mesh`, and the `meshAssetId` check is now an
     `outputType == Mesh` ⟺ `meshAssetId != 0` consistency check instead of an unconditional ban.
     `EditorParticleBakeHostTests.cpp`'s existing "Mesh is unsupported" case was swapped to `Trail`; new
     positive (valid mesh reference bakes successfully) and negative (unregistered mesh reference is
     rejected) cases were added.
- Independent focused matrix after all fixes — exit `0`, `7/7`, `10.95 s`:
  `ctest --test-dir build -C Debug -R "^(kb_21kb_particle_asset_tests|kb_21kb_particle_cpu_backend_tests|kb_21kb_particle_editor_tests|kb_21kb_particle_snapshot_tests|kb_21kb_particle_renderer_tests|kb_editor_particle_authoring_tests|kb_editor_particle_bake_host_tests)$" --output-on-failure`.
  Individual: asset `3.55 s`, snapshot `0.19 s`, CPU `5.98 s`, editor core `0.68 s`, GPU renderer `0.48 s`,
  authoring layout `0.02 s`, host Bake `0.03 s`.
- `cmake --build build --config Debug --target kb_editor` — exit `0`, confirming nothing else in the
  editor/renderer/engine link graph was broken by these changes.
- **Not yet true**: Mesh-output particles do not render anything yet — no code submits mesh instances to
  the GPU. The capability gate being open means an author can now save/Bake a Mesh-output effect without
  an error, but it will render as nothing until package 2 lands. This is a deliberate, tracked intermediate
  state of a single in-progress stage, not a claim that Stage 8 is done.

### Stage 8 package 2 of 2 — mesh instance submission: implemented, partially verified

Mesh-output particles now actually render through the real GPU mesh pipeline. Design confirmed by
investigation before writing code: full reuse of the existing non-particle instanced-mesh pipeline is
possible and is what the audit itself specifies ("bez mesh proxy map") — `MeshPipelineProcessor::BuildInto`/
`SceneMeshDrawCommandSubmitter::Submit` do not require input sourced from `RenderScene`'s per-entity mesh
proxies; `MeshPipelineBuildDesc.meshBatches` is accepted independent of `RenderScene` entirely, and program
selection is driven entirely by the `RenderMaterial`'s own shader assignment — no new shaders needed.

- New `sources/renderer/include/kb/render/particles/ParticleMeshBatchBuilder.hpp` / `src/particles/ParticleMeshBatchBuilder.cpp`:
  builds `SceneMeshBatch`/`SceneRenderMeshInstance` values directly from Mesh-output emitter records and
  their particles in a `ParticleRenderSnapshot` — one batch per Mesh-output emitter (multiple particles of
  the same emitter already batch into few draws via the reused pipeline's own instancing; merging identical
  mesh/material *across different* emitters is a possible future optimization, not attempted here). Built
  with a two-pass count-then-fill approach specifically to avoid a real hazard: `SceneMeshBatch::instances`
  is a `std::span` into the builder's own growing vector, so a mid-build reallocation would leave every
  earlier batch's span dangling; capacity is reserved to the exact total before any span is taken.
  Per-instance model matrix: `kb::math::FromTRS(particle.position, emitterBasis * spin(particle.rotationRadians), uniformScale)`,
  reusing existing `FromTRS`/`Quat` operator* / the same snorm-unpack formula `ParticleRenderBatcher.cpp`
  already uses for billboard alignment (`value/32767`) — no new math. **Documented scope boundary**: the CPU
  sim only tracks a single scalar spin per particle (`rotationRadians`, the same value billboards use), not
  a full 3D orientation; mesh instances get the emitter's authored local orientation plus that spin around
  its local Z axis, not independent per-particle 3D tumbling. A future stage adding real per-particle 3D
  orientation would need a new `ParticleRenderRecord` field, not a fix here.
- `SceneMeshSubmitter::Submit()` (`sources/renderer/src/scene/SceneMeshSubmitter.cpp`) now runs a second,
  independent `MeshPipelineProcessor::BuildInto` + `SceneMeshDrawCommandSubmitter::Submit` call whenever a
  particle snapshot is supplied, submitting `particleMeshBatchBuilder_`'s batches for the *current* pass —
  letting the existing per-pass policy (`MeshPipelinePassPolicy`) decide opaque/transparent/shadow
  participation exactly as it already does for ordinary meshes, rather than hand-routing by blend mode.
  `SceneRenderer.cpp`'s call site was changed to pass the particle snapshot for *every* pass (previously
  gated to `BaseTransparent` only, matching the pre-existing quad-billboard `particleRenderer_` path, which
  stays `BaseTransparent`-only and unchanged) — mesh particles need `ShadowDepth`/`BaseOpaque`/`GBuffer` too.
  **Documented scope boundary**: particle-mesh draw commands are submitted as their own call, not merged into
  the shared mesh/quad-particle transparent depth-sort queue (`TransparentDrawOrderEntry`) — they render with
  correct per-material blend/depth state but are not perfectly camera-depth-interleaved against unrelated
  mesh/billboard draws in the same pass in this first cut. Extending the shared queue with a third source
  kind is future work, not a silent bug.
- New test `TestMeshBatchBuilderInstancesLodShadowAndExclusion` (`sources/renderer/tests/ParticleRendererTests.cpp`)
  verifies: a Mesh emitter's particles produce exactly one batch with the right mesh/material IDs and
  instance count; a Billboard emitter in the same snapshot produces none; each instance carries the right
  `entityId`/`castsShadow`/`receivesShadow`/`lodBias`; the model matrix places an identity-oriented,
  unit-scale particle at its authored position (translation in indices [12,13,14], matching `FromTRS`'s
  column-major layout, confirmed against `vs_mesh_instanced.sc`'s `i_data0..3` convention); color unpacks
  from the packed particle color; an all-non-Mesh snapshot produces zero batches. The test also supplies a
  real two-LOD mesh resource to `MeshPipelineProcessor`: three instances with `lodBias == 1` produce exactly
  one command for LOD 1, and the same three shadow-casting instances produce one ShadowDepth command.
- Follow-up integration repair: particle-mesh batches are appended to the scene batches before the single
  `MeshPipelineProcessor::BuildInto` call, rather than using a second pipeline build/submit. That makes
  draw/visible-instance budgets apply across both sources, preserves aggregate command statistics, and puts
  transparent mesh particles into the existing shared mesh/particle depth-order queue.
- `RunRendererSubmitsParticleMeshSnapshotAsOneDrawTest` (`RendererRuntimeSubmitTests.cpp`) publishes a real
  Mesh snapshot through `ParticlePlayback`, resolves a real mesh/material asset, and asserts through the
  runtime scene submission path that three instances of one mesh/material submit as one draw. It repeats
  publish -> submit -> `ReleaseScene` 100 times and asserts the mesh resource is released every cycle.
- Verification performed: full focused matrix (7 targets, listed below) green; `kb_editor` full dependency
  graph rebuild green; **the real headless editor automation scenario** (`kb_editor_particle_authoring_headless`,
  which drives the actual `kb_editor.exe` and its live bgfx headless rendering path) passes, exercising the
  new `SceneMeshSubmitter`/`SceneRenderer` integration end-to-end without crashing.
- **Not yet verified — genuinely open, not silently claimed done**: (1) no test asserts an actual GPU draw
  *count* through `SceneMeshDrawCommandSubmitter` for "N particles of one mesh/material = one draw per
  section/LOD" — this requires registering a real `RenderMeshResource`/`RenderMaterialResource` fixture in a
  `RenderResourceRegistry`, a test pattern used elsewhere (`MeshPipelineTests.cpp`) but not yet adapted here;
  (2) no test confirms `meshLodLevel`/`lodBias` actually selects the correct mesh section/LOD (only that the
  value is correctly threaded into the instance); (3) no dedicated hot-reload/scene-release stress test for
  this specific new path (the architecture has no persistent per-scene resource ownership of its own —
  batches are transient, rebuilt every `Submit()` call from the snapshot, so there is nothing stage-specific
  to leak by construction, but this claim has not been proven by a 100-cycle test the way other stages'
  lifetime guarantees were); (4) no visual/golden coverage. These four remain before Stage 8's gate (audit
  section 11) can be marked `accepted`.
- **Superseding verification update (2026-08-20)**: items (1), (2), and (3) above are closed by the
  focused renderer tests documented immediately before this paragraph. The only remaining Stage 8 gate item
  is visual pixel-readback/golden coverage through the complete runtime scene path; the headless Noop backend
  proves command submission but cannot prove final pixels.
- **Final acceptance update (2026-08-20)**: `RunRendererDrawsParticleMeshSnapshotPixelsTest` uses a native
  hardware rendering surface and an offscreen RGBA8/depth target. It publishes the same real Mesh snapshot,
  confirms three instances submit as one draw, reads the target back, and requires at least 32 pixels to differ
  from the clear color. The fixture material is explicitly double-sided so the visual assertion is independent
  of imported triangle winding. This closes the visual-readback gate; the current `SceneMeshSubmitter` path
  combines ordinary and particle mesh batches before one pipeline build, so its budget, command statistics,
  section/LOD grouping, and transparent order are shared.

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

### Stage 5 — GPU particle renderer baseline: `accepted`

- The renderer consumes immutable snapshots directly, replaces proxy-per-particle rendering with GPU-instanced compact batches, and keeps `RenderScene::MeshProxyCount` independent of particle count.
- Billboard, stretched billboard, point sprite, flipbook, soft-depth fade, six blend modes including exact Subtractive semantics, per-view alignment, and deterministic sort policies are implemented in renderer-owned shaders and resources across all staged shader profiles.
- Mesh and particle transparent draws share one camera-depth key and queue; supported mixed-effect emitters continue rendering while unsupported outputs generate typed per-emitter diagnostics and drops.
- Two viewport consumers read the same snapshot revision without repeating simulation. Warmed batch construction is allocation-free; transient instance limits are split or reported as drops.
- The core owns snapshot allocation before dynamic provider attachment, and unused ECS query plans are released before module unload. The real DLL regression passes 100 reloads plus scene destruction with and without scripts.

### Stage 6A — document core and shared runtime preview: `accepted checkpoint`

- Plugin-local, platform-neutral document state has canonical bounded history, an explicit save point, strict atomic save semantics, and a pure dirty-transition guard for open, revert, tab/window/project close, and application exit.
- The isolated preview publishes an unsaved working copy as a runtime asset in a real runtime scene with the enabled provider, normal `SceneRuntime`, and the accepted GPU renderer; it has no second accumulator or simulation kernel.
- First-save session paths commit only after successful atomic IO. Reopen, undo/redo/revert, save failure preservation, unsaved preview no-disk-write, provider-backed runtime snapshots, Noop GPU submit, and scene/backend/snapshot/renderer teardown are covered by the dedicated focused suite.
- Host panel/docking routing and Bake remain deliberately outside 6A. Bake requires a future public core-owned immutable compiled-effect artifact rather than access to private CPU backend state.

### Stage 6B — host panel and preview lifecycle: `accepted checkpoint`

- The editor hosts panel 14, `21kb Particle System`, in the center document workspace with dock/floating behavior, asset activation, shared preview presentation, and close routes that all use the 6A dirty guard.
- The host owns no simulation path: its viewport release callback clears renderer scene state before the accepted preview session destroys its runtime scene and provider. Resize, docking, and floating preserve the existing preview session and camera.
- A bounded, strict, atomic in-project session store preserves panel visibility, placement, floating rectangle, and session path. The editor tests cover panel presence, layout movement, persistence, and escape rejection.
- Full authoring controls remain outside 6B; production Bake is delivered separately in Stage 6C1.

### Stage 6C1 — shared compiler and production Bake: `accepted checkpoint`

- `kb_engine` owns the immutable `ParticleCompiledEffect` value and its const handle. The provider and editor use one static compiler rather than reaching into the CPU backend's private compiled representation; backend generation/LKG entries retain that handle.
- The compiled cache is field-wise, checksummed, bounded to 512 KiB, versioned, verified after atomic replacement, and keyed by canonical working-copy bytes, sorted transitive dependency metadata, compiler version, target platform, and renderer capability flags. It never writes the source asset.
- Dependency validation accepts an unsaved working asset while preserving loader publication of direct edges only; its separately exposed transitive result is deterministic and bounded.
- `EditorSceneContext::BakeParticleEditorAsset()` is the production host path. It compiles the current working copy into `<projectRoot>/Saved/21kbParticleCache`, leaves document, preview, and source bytes untouched, and reports every typed diagnostic to the editor console.
- Mesh, trail, ribbon, beam, volumetric, external-effect events, and GPU-required policy are rejected explicitly by the current compiler/Bake capability gate. They are never silently downgraded or cached as a supported artifact.
- Full authoring controls remain Stage 7 work; the accepted Bake command is intentionally a thin host operation, not a second panel workflow.

### Stage 7A — emitter workspace and interaction: `accepted checkpoint`

- Stable emitter IDs remain physically sorted and reference-safe. The persisted `authoringOrder` field is a validated contiguous permutation; missing v2 fields map deterministically to the existing vector order, while the canonical writer always emits it. The shared compiler executes that authored order and resolves event references by stable ID.
- The editor now has a typed emitter workspace and commands for selection, rename, enable, reorder, removal, and valid material-backed creation. Reorder drags mutate only on release and produce one document-history entry; preview publication failure rolls the document/workspace back to the prior valid state.
- The 2:1 particle workspace renders a DPI-scaled, scrollable emitter stack with status, limit feedback, keyboard controls, hit testing, and docked/floating routing. The source-backed material picker is revalidated by the host; cancellation makes no mutation and the 8-emitter ceiling disables Add before a document mutation.
- Automation recognizes `ParticleEffect` assets and supports a bounded, catalog-validated `initial_plugins` setup before editor construction. The editor target stages the current provider DLL beside its executable, so portable normalized project descriptors resolve the current binary rather than a stale neighbor.
- Focused visual automation passed all eight steps and produced six docked/floating/DPI BMPs. Manual inspection confirmed the expected authoring layout at 1366×768 docked and 150-DPI floating. Module, output, curve, gradient, recipe, picker, and diagnostic-navigation controls remain later Stage 7 work.

### Stage 7B — module and output-property authoring: `accepted checkpoint`

- Typed module and output-property authoring is live over the accepted Stage 7A emitter workspace: typed property rows cover spawn, output, and module-payload fields; output-choice rows carry per-type diagnostics; the module stack supports add/toggle/reorder/select through `ParticleEditorCommands`; dependency rows and diagnostic rows expose capability states, with `NavigateDependency` wired end-to-end to the Asset Browser. Curve, gradient, and recipe-category authoring landed as follow-ups within this same checkpoint — see below.
- All nine module types (`InitialVelocity`, `Gravity`, `Wind`, `Drag`, `ColorOverLife`, `SizeOverLife`, `AlphaOverLife`, `CollisionPlane`, `SubEmitter`) can be added to a valid document/history transaction with bounded, structurally valid defaults. `ColorOverLife` now defaults to a two-stop opaque-white gradient instead of an empty (structurally invalid) one. `SubEmitter` requires an explicit, different target emitter: the Add Module menu disables the Sub Emitter entry when fewer than two emitters exist, a follow-up popup collects the target from the other emitters, and the command rejects a missing/self/unknown target before mutating history.
- Two defects independent of the two tracked gaps were found and repaired while re-verifying this checkpoint in a fresh checkout of this session: (1) `kb_editor` failed to compile (`error C2039: 'targetEmitterId' is not a member of 'kb::editor::ParticleEditorPanelHit'`, `EditorLeftButtonDownRouter.cpp:543`) because the Sub Emitter target-selection commit added `targetEmitterId` to `ParticleEditorEmitterRowLayout` but not to `ParticleEditorPanelHit`, which the router and `ParticleEditorPanelInteraction::Execute` actually populate and read; the focused unit suite never caught this because it exercises `ParticleEditorCommands` directly and never re-links `kb_editor`. Repaired by adding the missing field. (2) `kb_21kb_particle_asset_tests` failed (`hand-authored canonical fixture is not byte stable`) because this Windows checkout's `core.autocrlf=true`, with no `.gitattributes` override, smudged the LF-only committed `CanonicalParticleEffectV2.kbvfx` fixture to CRLF on disk; the writer is unconditionally LF-only, so the round-trip comparison could never match. Repaired by normalizing the working copy back to LF (no content change; the committed blob was already LF) and adding `.gitattributes` to pin that path to `text eol=lf` so a future checkout in this or any other environment cannot silently reintroduce the corruption.
- Focused asset, CPU backend, editor-core, authoring-layout, and headless-authoring gates passed together after both repairs, with `kb_editor` rebuilt and re-linked.
- Follow-up within this checkpoint: curve and gradient editing (the two "explicit capability states" items that stood in for real editors above) are now real, not opaque. `ColorOverLife`'s gradient, `SizeOverLife`/`AlphaOverLife`'s curves, and the per-emitter spawn rate curve are exposed as editable property rows through the same typed value-edit dialog every other property already uses, encoded as a compact `time,value,easing`-per-keyframe (`;`-separated) / `time,r,g,b,a`-per-stop text the shared dialog can round-trip; ordering, range, and count limits are enforced centrally by `ParticleEffectAssetValidator::ValidateStructure` (run by every `Apply()`), not duplicated in the parser. `ParticleEmitterInspectorModel` formats the rows; `EditorSceneContext::EditParticleEditorProperty` (`EditorParticleEditorHost.cpp`) parses and commits them through the existing `SetModulePayload`/`SetEmitterSpawn` commands — no new command types or panel actions were needed.
- Second follow-up: recipe browsing/categorization is closed. Recipes are ordinary `ParticleEffect` assets, so a dedicated recipe UI would have duplicated the Asset Browser; instead `recipeCategory` (already validated and round-tripped since Stage 1B) is now surfaced there. A full `AssetManager::Load<T>()` per row was rejected — it unconditionally reloads from disk and would force-parse every recipe on every browser refresh/keystroke. Instead added an optional `IAssetLoader::DiscoverBrowseTag(path)` hook (default empty, mirroring the existing `DiscoverDependencies`/`ValidateDependencies` no-op-default pattern), called once per file during `AssetDiscoveryService::DiscoverMountedAssets` and skipped when the file's content hash is unchanged since the previous scan (a genuinely cheap steady-state cost, not just a "cheaper than Load" one — see the code-review entry below for why). `ParticleEffectAssetLoader` overrides it with one `LoadDetailed` call and returns `recipeCategory`. The result lands in `AssetMetadata.browseTag`, a dedicated free-text field folded into `EditorAssetBrowserAssetRows::SearchText()` — deliberately *not* `importCategory`, a closed, trusted vocabulary ~12 editor call sites match by equality (mesh/texture drag classification, icon resolution, the `"EditorLiveOverride"` live-reload sentinel); see the code-review entry for why that distinction is load-bearing, not cosmetic.
- Independent code review pass (`code-review high --fix`, 10 finder angles) over this whole session's diff (`2e588553..HEAD`) found and all-fixed: (1) **correctness/security** — the recipe hook originally wrote into `AssetMetadata.importCategory` itself, a field treated elsewhere as a closed, trusted classification vocabulary; an author-chosen `recipeCategory` colliding with a reserved value (e.g. `"Mesh"`, `"Texture"`, or the `"EditorLiveOverride"` sentinel) could have misclassified a `ParticleEffect` as scene-placeable/texture-draggable, or frozen that asset's metadata against future edits. Fixed by giving it its own dedicated `AssetMetadata::browseTag` field instead of sharing `importCategory`. (2) **efficiency** — `DiscoverBrowseTag` (called `DiscoverImportCategory` before this fix) ran unconditionally on every discovery scan, fully re-parsing every unchanged `.kbvfx` file; `AssetDiscoveryService::DiscoverMountedAssets` also runs from a throttled but live runtime path (`RuntimeRenderAssetDiscovery`, default every 30 frames, enabled by default outside the editor), so this was a real recurring cost, not a one-off editor refresh. Fixed by reusing the already-computed content hash to skip the hook (and reuse the previous scan's tag) when a file is unchanged. (3) **correctness** — `CurveText`/`GradientText` used `std::to_string(float)`, a fixed-6-decimal, locale-sensitive format, while `ParseCurve`/`ParseGradient` explicitly forced the classic locale; besides the general precision loss, two keyframe/stop times differing only past the 6th decimal could round to identical text, making a previously-valid curve un-committable from merely opening and confirming its own edit dialog. Fixed by switching both to a `std::to_chars`-based round-trip-safe, locale-independent formatter matching the engine's own `ParticleEffectAssetIO.cpp` `Float()`. (4) **robustness** — `ParseCurve`/`ParseGradient` rejected a single trailing `;` (a plausible fat-finger) as fully malformed, accumulated unbounded keyframes/stops before checking the schema count limit, and parsed the easing index into an unsigned type that let a leading `-` wrap into a valid-looking enum value instead of being rejected. Fixed: one trailing `;` is now tolerated, the count limit is checked per-iteration (early exit), and easing is parsed signed and explicitly range-checked including sign. (5) **UX** — the shared value-edit dialog always showed a vector/scalar hint ("Use numbers like: 0.25 or 1 0 0 1") even for curve/gradient rows needing the new `time,value,easing;...`/`time,r,g,b,a;...` grammar; the dialog gained an optional context-specific hint parameter (default unchanged for its ~6 other call sites), and the particle property call site now passes the right one. (6) **naming** — `ParticleEditorProperty::SpawnRateSummary` was renamed to `SpawnRateCurve`; the `*Summary` suffix elsewhere on this enum means "read-only aggregate count" (its neighbor `SpawnBurstsSummary` still is exactly that) and this field is no longer that. Not fixed, noted as accepted/out of scope: `ParseCurve`/`ParseGradient`/`CurveText`/`GradientText` still duplicate their tokenizer skeleton across two functions/one file (style/maintainability, not correctness); `AssetManifest.cpp`'s tab-separated serialization does not carry `browseTag` (verified dead code today — no production call site reads or writes through `AssetManifest`, only its own tests do — so nothing currently round-trips through it, but a future manifest-based cache loader would need to add it, mirroring `importCategory`).
- Full focused matrix re-verified after all review fixes: `cmake --build build --config Debug --target kb_engine` — exit `0`. `cmake --build build --config Debug --target kb_21kb_particle_asset_tests kb_21kb_particle_editor_tests --parallel 2` — exit `0`; both executables exit `0`. `cmake --build build --config Debug --target kb_editor` — exit `0`. `ctest --test-dir build -C Debug -R "^(kb_21kb_particle_asset_tests|kb_21kb_particle_cpu_backend_tests|kb_21kb_particle_editor_tests|kb_editor_particle_authoring_tests|kb_editor_particle_authoring_headless)$" --output-on-failure` — exit `0`, `5/5`, `13.84 s`.

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
- Stage 6C1 adds the engine compiled-effect/cache sources, shared plugin compiler, production Bake service, host command, and their focused asset, CPU, editor, and host regressions.
- Stage 7A adds emitter workspace/model/commands, panel layout/interaction/routing, automation setup, provider staging, and focused authoring/headless regressions.
- Stage 7B adds module/output-property authoring, the `ColorOverLife` default gradient, and the `SubEmitter` target-selection flow (schema, editor commands, panel interaction/routing, focused asset/editor tests). This session additionally touches `sources/editor/src/private/rendering/ParticleEditorPanelLayout.hpp` (`ParticleEditorPanelHit.targetEmitterId`, closing a `kb_editor` compile break) and adds root `.gitattributes` (pins `sources/engine/tests/fixtures/CanonicalParticleEffectV2.kbvfx` to LF against `core.autocrlf` checkout corruption).
- Curve/gradient editing follow-up touches `sources/plugins/21kb_particle/editor/ParticleEmitterInspectorModel.cpp` (`CurveText`/`GradientText` formatting, editable rows), `sources/editor/src/scene/EditorParticleEditorHost.cpp` (`ParseCurve`/`ParseGradient`, dispatch wiring for `ColorOverLife`/`SizeOverLife`/`AlphaOverLife`/`SpawnRateSummary`), and `sources/plugins/21kb_particle/tests/ParticleEditorTests.cpp` (inspector-row and command-level round-trip coverage).
- Recipe browsing follow-up touches `sources/engine/include/engine/assets/IAssetLoader.hpp` (`DiscoverBrowseTag` hook), `sources/engine/include/engine/scene/ParticleEffectAssetLoader.hpp`/`.cpp` (override), `sources/engine/src/assets/AssetDiscoveryService.cpp` (wiring into the existing discovery scan), and `sources/engine/tests/ParticleEffectAssetTests.cpp` (`RunRecipeAssetTest` extended to assert the discovered category for all 15 recipes).
- The subsequent code-review fix pass additionally touches `sources/engine/include/engine/assets/AssetMetadata.hpp` (`browseTag` field, separate from `importCategory`), `sources/editor/src/assets/EditorAssetBrowserAssetRows.cpp` (`SearchText` folds in `browseTag`), `sources/plugins/21kb_particle/editor/ParticleEmitterInspectorModel.cpp`/`.hpp` (`FloatText`, `SpawnRateCurve` rename), `sources/editor/src/scene/EditorParticleEditorHost.cpp` (`ParseCurve`/`ParseGradient` robustness, `SpawnRateCurve` rename), `sources/editor/src/private/platform/win32/EditorMaterialParameterValueDialog.hpp`/`sources/editor/src/platform/win32/EditorMaterialParameterValueDialog.cpp` (optional hint parameter), and `sources/editor/src/app/pointer/EditorLeftButtonDownRouter.cpp` (curve/gradient-specific hint text).
- Stage 8 package 1 (data plumbing) touches `sources/engine/include/engine/particles/ParticleCompiledEffect.hpp` (new `ParticleCompiledEmitter` mesh fields), `sources/engine/include/engine/particles/ParticleRenderSnapshot.hpp` (new `ParticleRenderEmitterRecord`/`ParticleRenderEmitterFlag` fields), `sources/engine/src/particles/ParticleRenderSnapshot.cpp` (flags allowlist fix), `sources/engine/src/particles/ParticleCompiledEffectCache.cpp` (cache serializer: new fields, output ceiling, `meshAssetId` consistency check), `sources/engine/tests/ParticleRenderSnapshotTests.cpp` (new field coverage), `sources/plugins/21kb_particle/ParticleEffectCompiler.hpp`/`.cpp` (capability gate, latent variant-access bug fix), `sources/plugins/21kb_particle/CpuParticleBackend.cpp` (populates the new fields, Mesh-gated), `sources/renderer/src/particles/ParticleRenderBatcher.cpp` (Mesh exclusion from the quad batcher), `sources/renderer/tests/ParticleRendererTests.cpp` (stale exemplar swap, new exclusion test), and `sources/editor/tests/EditorParticleBakeHostTests.cpp` (stale exemplar swap, new positive/negative Mesh Bake cases).
- Stage 8 package 2 (mesh instance submission) adds `sources/renderer/include/kb/render/particles/ParticleMeshBatchBuilder.hpp` and `sources/renderer/src/particles/ParticleMeshBatchBuilder.cpp` (new), touches `sources/renderer/CMakeLists.txt` (registers the new source), `sources/renderer/src/scene/SceneMeshSubmitter.hpp`/`.cpp` (second `MeshPipelineProcessor::BuildInto`/`SceneMeshDrawCommandSubmitter::Submit` call for particle-mesh batches, every pass), `sources/renderer/src/scene/SceneRenderer.cpp` (passes the particle snapshot for every pass, not just `BaseTransparent`), and `sources/renderer/tests/ParticleRendererTests.cpp` (`TestMeshBatchBuilderInstancesLodShadowAndExclusion`).

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
- Stage 6C1 final focused CTest matrix — exit `0`, `5/5`, `22.71 s`: asset `1.43 s`, DLL lifecycle `17.71 s`, CPU backend `3.35 s`, editor core `0.20 s`, host Bake `0.01 s`.
- Stage 6C1 host integration build — `cmake --build build --config Debug --target kb_editor -- /m:1`, exit `0`; the focused host target also built exit `0`.
- Stage 7A independent focused CTest matrix — exit `0`, `5/5`, `5.95 s`: asset `1.59 s`, CPU `3.81 s`, editor core `0.24 s`, authoring layout `0.02 s`, headless authoring `0.30 s`.
- Stage 7A editor integration build — `cmake --build build --config Debug --target kb_editor -- /m:1`, exit `0`, `6.8 s`; the post-build provider copy hash matched the provider target.
- Stage 7B verification, commit `2e588553` on top of the `b8c6de26` WIP snapshot: `cmake -S . -B build` — exit `0` (reconfigure to register test targets absent from the stale build cache).
- `cmake --build build --config Debug --target kb_21kb_particle_asset_tests kb_21kb_particle_cpu_backend_tests kb_21kb_particle_editor_tests kb_editor_particle_authoring_tests --parallel 4` — exit `0`.
- First `ctest --test-dir build -C Debug -R "^(kb_21kb_particle_asset_tests|kb_21kb_particle_cpu_backend_tests|kb_21kb_particle_editor_tests|kb_editor_particle_authoring_tests)$" --output-on-failure` — `kb_21kb_particle_asset_tests` failed (`hand-authored canonical fixture is not byte stable`); the other three passed. Root-caused to `core.autocrlf` checkout corruption of the LF-only fixture; repaired per the Stage 7B entry above.
- `build/engine/Debug/kb_21kb_particle_asset_tests.exe` after the LF fixture fix — exit `0`, "21kb Particle System asset tests passed".
- `cmake --build build --config Debug --target kb_editor -- /m:1` — failed, `error C2039: 'targetEmitterId': nie jest składową elementu 'kb::editor::ParticleEditorPanelHit'` at `EditorLeftButtonDownRouter.cpp(543,25)`; root-caused and repaired per the Stage 7B entry above.
- `cmake --build build --config Debug --target kb_editor` after the `ParticleEditorPanelHit` fix — exit `0`.
- Final Stage 7B independent focused CTest matrix — `ctest --test-dir build -C Debug -R "^(kb_21kb_particle_asset_tests|kb_21kb_particle_cpu_backend_tests|kb_21kb_particle_editor_tests|kb_editor_particle_authoring_tests|kb_editor_particle_authoring_headless)$" --output-on-failure`, exit `0`, `5/5`, `10.77 s`: asset `3.41 s`, CPU `5.81 s`, editor core `0.47 s`, authoring layout `0.03 s`, headless authoring `1.03 s`.
- Curve/gradient editing follow-up: `cmake --build build --config Debug --target kb_21kb_particle_editor_tests` — exit `0`; `build/21kb_particle/Debug/kb_21kb_particle_editor_tests.exe` — exit `0`, "21kb Particle System editor core tests passed".
- `cmake --build build --config Debug --target kb_editor` — exit `0` (compiles `ParseCurve`/`ParseGradient` and the new dispatch cases in `EditorParticleEditorHost.cpp`).
- Repeat of the full focused Stage 7B CTest matrix — exit `0`, `5/5`, `10.53 s`: asset `3.30 s`, CPU `5.93 s`, editor core `0.42 s`, authoring layout `0.03 s`, headless authoring `0.83 s`.
- Recipe browsing follow-up: `cmake --build build --config Debug --target kb_engine` — exit `0`; `cmake --build build --config Debug --target kb_21kb_particle_asset_tests` — exit `0`; `build/engine/Debug/kb_21kb_particle_asset_tests.exe` — exit `0`, all 15 recipes' `DiscoverImportCategory` matched their canonical browser category.
- `cmake --build build --config Debug --target kb_editor` — exit `0` (full dependency graph, including every other `IAssetLoader` implementation across engine/renderer/plugins, rebuilt clean against the new virtual hook).
- Repeat of the full focused Stage 7B CTest matrix — exit `0`, `5/5`, `10.82 s`.
- Broader regression check (this change touches shared `IAssetLoader`/`AssetDiscoveryService`, not just particle-scoped files): `cmake --build build --config Debug --target kb_engine_tests` — exit `0`. `ctest --test-dir build -C Debug -R "^kb_engine_tests$" --output-on-failure` — exit `8`, `0/1`, `251.73 s`, two failures: `LIB-134 determinism rigs diverged: max abs difference=0.0121757` (`PhysicsSceneSystemTests.cpp`) and a component-registry/script-API count mismatch (`RunEngineLibraryComponentRegistryTest`, `EngineLibraryTests.cpp:2690-2695`, comparing `EngineLibraryComponentRegistry::Catalog().size()` against `ScriptSceneComponentApi::ComponentNames().size()`). Both assertions are in physics-determinism and script-component-catalog code with no dependency on `IAssetLoader`/`AssetDiscoveryService`/`ParticleEffectAssetLoader`/asset discovery in general, confirmed by reading both failing checks directly — neither touches, calls, or is called by anything this session changed. Read as pre-existing and unrelated to the particle system work in this ledger; left unfixed as out of scope (matching this session's explicit instruction not to chase CI/broader-suite issues while particle implementation is in progress). The particle-scoped focused matrix above is unaffected and green.
- Stage 8 package 1 (capability gate + data plumbing): `cmake --build build --config Debug --target kb_engine` — exit `0`. Three regressions found and fixed in sequence, each independently rebuilt and reverified (see the package-1 ledger entry for detail): `kb_21kb_particle_cpu_backend_tests.exe` failing → fixed the `.flags` allowlist in `ParticleRenderSnapshot.cpp` → passing; `kb_21kb_particle_renderer_tests.exe` failing (`"one unsupported emitter hid supported GPU particle batches..."`) → swapped the stale `Mesh` exemplar to `Trail`, added a dedicated exclusion test → passing; `kb_editor_particle_bake_host_tests.exe` failing (`"Mesh Bake rejected: ... rebuilt compiled particle effect cache failed verification"`) → fixed `ParticleCompiledEffectCache.cpp`'s output ceiling and `meshAssetId` consistency check → passing. Final independent focused matrix — `ctest --test-dir build -C Debug -R "^(kb_21kb_particle_asset_tests|kb_21kb_particle_cpu_backend_tests|kb_21kb_particle_editor_tests|kb_21kb_particle_snapshot_tests|kb_21kb_particle_renderer_tests|kb_editor_particle_authoring_tests|kb_editor_particle_bake_host_tests)$" --output-on-failure`, exit `0`, `7/7`, `10.95 s`. `cmake --build build --config Debug --target kb_editor` — exit `0`.
- Stage 8 package 2 (mesh instance submission): `cmake --build build --config Debug --target kb_renderer` — exit `0` (new `ParticleMeshBatchBuilder.cpp` compiled standalone first, before wiring). `cmake --build build --config Debug --target kb_renderer_tests kb_21kb_particle_renderer_tests` — exit `0`; `build/renderer/Debug/kb_21kb_particle_renderer_tests.exe` — exit `0` including the new `TestMeshBatchBuilderInstancesLodShadowAndExclusion`; `kb_renderer_tests.exe mesh-pipeline` — exit `0`; `kb_renderer_tests.exe scene-sync` — exit `0`. After wiring into `SceneMeshSubmitter`/`SceneRenderer`: full focused matrix repeat — exit `0`, `7/7`, `14.81 s`. `cmake --build build --config Debug --target kb_editor` — exit `0`. `ctest --test-dir build -C Debug -R "^kb_editor_particle_authoring_headless$" --output-on-failure` — exit `0`, `1/1`, `3.16 s` — the real headless `kb_editor.exe` automation scenario, exercising live bgfx headless rendering through the new `SceneMeshSubmitter` integration, not just unit tests.

### Stage 9 — trail, ribbon, and beam output: accepted

- The compiled-effect contract and cache format now carry bounded trail cadence/distance/ring fields,
  ribbon segment/width/break policy, and authored beam endpoint/noise fields. The compiler capability
  key, cache reader/writer, and snapshot validation all gate the new data; invalid ring, segment, endpoint,
  and non-finite values fail before rendering.
- CPU snapshot publication preserves a stable spawn ordinal and ribbon group through every compaction path,
  publishes world-space beam endpoints bound to the emitter transform, and preserves the fixed simulation
  step used for beam noise.
- `ParticleStripGeometryBuilder` owns fixed-capacity scratch storage: 16,384 trail histories, 65,536
  vertices, and 98,304 indices. History is keyed by scene, backend epoch, and particle identity, preventing
  cross-scene collisions and stale samples after a renderer release or backend restart. Capacity exhaustion
  returns an explicit status and exact dropped-segment count.
- `ParticleStripRenderer` owns dynamic GPU vertex/index buffers and submits Trail, Ribbon, and Beam draws
  through the transparent order shared with mesh and billboard particles. Scene statistics now separately
  report strip submitted/dropped segments, failed strip batches, and upload bytes; scene and global release
  clear retained history.
- Verification: `ctest --test-dir build -C Debug -R "^(kb_21kb_particle_asset_tests|kb_21kb_particle_cpu_backend_tests|kb_21kb_particle_editor_tests|kb_21kb_particle_snapshot_tests|kb_21kb_particle_renderer_tests|kb_editor_particle_authoring_tests|kb_editor_particle_bake_host_tests)$" --output-on-failure` — exit `0`, `7/7`, `11.70 s`.
  `kb_renderer_tests.exe particle-strip-submit`, `mesh-pipeline`, and `scene-sync` each exit `0`; the strip
  submit test includes native offscreen pixel readback. Geometry tests cover compact-order preservation,
  ribbon discontinuities, fixed-step beam output, camera crossing, endpoint motion, backend epoch restart,
  and hard trail/GPU buffer limits.

## Rejected attempts, open defects, and next gate

- **Current Stage 8 status (2026-08-20): accepted.** This supersedes the historical in-progress notes below:
  wrong/missing mesh compile/Bake rejection was closed in package 1; the focused mesh-pipeline test covers
  one draw per selected LOD and the shadow pass; the runtime test performs 100 publish/submit/release cycles;
  and the native offscreen readback test proves final mesh pixels. Stage 9 is accepted above.
- The first controlled CPU-golden run intentionally used an impossible expected hash and failed with actual `14160509741928470306`; the exact observed value is now frozen and passes.
- The current cone distribution, upper-seed truncation, and successful no-op emit are characterized defects scheduled for runtime replacement, not accepted target behavior.
- Stage 1A attempt 1 was rejected after independent review despite a green focused test; every listed defect was repaired and the second attempt was accepted.
- No open stage 0 defect remains.
- No open stage 1 defect remains.
- No open defect remains in Stages 0 through 6 or Stage 7A.
- The previously open Stage 7B gap — `ColorOverLife` had no valid default gradient and `SubEmitter` had no explicit target-selection flow, so those two module menu entries could not create a valid history transaction — is closed. The default-gradient and target-selection-flow repair landed in commit `2e588553`; this session independently re-verified the all-nine-module add contract and found it holds (see Stage 7B evidence).
- Two additional, previously undetected defects were found while re-verifying Stage 7B in this session (a `kb_editor` compile break from the same commit, and a `core.autocrlf` checkout corruption of the byte-stability fixture) and are repaired; see the Stage 7B entry under Accepted stages for root cause and evidence.
- Curve and gradient editing (previously opaque, read-only summary rows with no dispatch case at all for `ColorOverLife`/`SizeOverLife`/`AlphaOverLife`/spawn rate) is closed; see the Stage 7B follow-up entry under Accepted stages.
- Dependency navigation (`NavigateDependency`) was independently code-read this session and found fully wired end-to-end (hit test → router → `ParticleEditorPanelInteraction::Execute` → `EditorSceneContext::NavigateParticleEditorDependency` → real `EditorAssetBrowserState::SelectAsset`, `EditorParticleEditorHost.cpp:426-430`) — not a defect. It reveals the referenced asset in the Asset Browser rather than opening a `ParticleEffect` dependency directly in the Particle Editor panel; that is standing, intended behavior, not a gap. No automated test exercises `NavigateParticleEditorDependency` itself yet (only that the layout hit test produces the right action/index); still open as a coverage gap, not a functional one.
- Recipe browsing/categorization is closed: `recipeCategory` is now discovered cheaply at scan time (`ParticleEffectAssetLoader::DiscoverImportCategory`) and surfaces through the Asset Browser's existing search; see the Stage 7B second follow-up entry under Accepted stages. Recipes remain ordinary `ParticleEffect` assets opened through the generic asset-open flow (no separate "apply recipe" merge command exists, and none was requested) — browsing/filtering by category was the concrete gap, and it is closed.
- No open defect and no unimplemented item remains from the original Stage 7 scope list (modules, output properties, curves, gradients, recipes, dependency navigation states).
- Stage 8 (mesh output) is `in_progress`, not `accepted`. Package 1 (capability gate, full data-path plumbing through schema/compiler/cache/CPU-backend/snapshot/batcher-exclusion) is closed and independently verified. Package 2 (actual GPU mesh instance submission, reusing the existing scene mesh pipeline) is implemented, compiles clean across the whole dependency graph, passes the full focused test matrix, and passes the real headless editor automation scenario end-to-end — but four items remain open before the audit's Stage 8 gate criteria (section 11) can be marked `accepted`: (1) no test asserts an actual GPU draw *count* ("N particles of one mesh/material = one draw per section/LOD" — the gate's own headline criterion); (2) no test confirms `meshLodLevel` actually selects the correct mesh section/LOD, only that the value is threaded through correctly; (3) no dedicated hot-reload/scene-release stress test for this specific new path; (4) no visual/golden coverage. See the Stage 8 package 2 ledger entry for exactly what would be needed to close each.
