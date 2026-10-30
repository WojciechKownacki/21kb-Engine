# Particle simulation

The particle provider selects simulation per emitter. `GpuVisualPreferred`
allows CPU simulation when an emitter needs behavior the GPU kernel cannot
preserve. CPU particles still use the renderer's particle draw path.

Billboard flipbooks use `columns` and `rows` (1–256 each), a zero-based
`startFrame`, and `frameCount`. A count of zero uses all remaining atlas cells;
an explicit count must fit after the first frame. This supports partially filled
atlases without displaying unused cells. Missing range fields retain the old
whole-atlas behavior. Frames advance with particle age at `framesPerSecond`;
zero speed holds the first frame, looping wraps within the range, and one-shot
playback holds the last frame until particle death. The editor exposes these
properties with Undo/Redo, and CLI paths use
`effect.emitter[0].output.payload.flipbook.startFrame` and `frameCount`.
CPU particle ages accumulate in double precision before conversion to render
records, avoiding accumulated float drift at frame and lifetime boundaries.

GPU billboard simulation evaluates the same frame range in both free-flight and
collision kernels. Cooking and the versioned compiled cache preserve the range.
If the maximum lifetime multiplied by playback speed exceeds 16,777,215 frames,
the provider uses CPU simulation with reason `flipbook-frame-precision` to avoid
loss of whole-frame precision in the GPU clock.

The particle editor's Diagnostics section reads the selected emitter's preview
snapshot. It shows the active simulation path, fallback reason and CPU render
record count and bytes. These records can include particles retained from a
previous path after a live edit. Missing snapshot data and unmeasured CPU/GPU
timings are shown as unavailable; authored policy is not used as a measurement.

Creating an effect in the editor or with `vfx-new` copies the shared default
material beside the effect under a unique name. The effect references that
project asset, so the material joins the normal cook dependency closure and
the cooked runtime can load it. Existing material files are never overwritten;
an explicitly assigned material keeps its authored reference.
Adding an emitter in the editor inherits the selected emitter's material unless
a material is explicitly chosen. This preserves project material dependencies
as an effect grows.

Each emitter can explicitly request `screenDepthCollision` (default `false`).
The particle editor exposes it as **Screen depth collision (GPU)**; CLI authors
can use `vfx-set --set effect.emitter[0].screenDepthCollision=true`. The source,
compiled artifact and versioned cache preserve the choice independently of
collision-plane modules. GPU simulation uses the renderer's available scene
depth through bgfx. CPU simulation and frames without usable scene depth
continue without this optional screen-space response; the compiler reports
that limitation as a warning. Collision planes retain their own behavior.

Screen-depth collision sees the camera's nearest opaque surfaces. It cannot
collide with geometry hidden behind an occluder. Explicit planes remain active
regardless of that visibility. The `kb_renderer_particle_depth_collision`
CTest checks these boundaries on the forward and deferred bgfx depth paths.
The motion/kernel readback tests also sample an actual 16x16 R32F GPU depth
fixture and require a bounce before removing that resource. After resetting the
births, ten steps without a depth handle must match the explicitly disabled
depth control and constant-velocity motion within 0.00001 m. Independently
authored killing planes must still produce empty instance records. Native
buffer capture is cross-checked against compute/texture readback; logical
emitter allocations return from 768 bytes to zero. This checks the simulation's
missing-resource boundary on D3D11, without forcing renderer allocation failure.

Collision planes with `killOnContact` end particle life on the GPU at contact.
The stateful bgfx kernel retains a death stamp and writes an empty render
instance immediately. Later dispatches, including removal of the plane during
live editing, retain that death. Uploading a new birth resets the slot state,
even when its timestamp matches the previous birth. Billboard and mesh output
use the same death handling. Explicit CPU simulation remains available.
An authored plane never implicitly enables
screen-depth collision; the renderer's explicit depth-collision parameter is
independent of the plane.

The GPU parameter block carries up to 16 active motion operations in authored
CPU execution order. Repeated Noise, AxisRotation and Attraction modules retain
their individual parameters. Acceleration and drag operations stay interleaved
with them, so mixed fields do not silently change order. Noise seeds retain all
32 bits through two exactly representable uniform values. Disabled modules do
not enter the motion stream. Field vectors remain in world axes, including for
local-space emitters under owner rotation and nonuniform scale.
Every eligible emitter sends a fixed-step clock update between births, so
impulse effects keep moving and reach their lifetimes. Missing stateful shader
support leaves the GPU consumer unavailable and retains CPU simulation.
The compiled cache preserves authored module and emitter order independently
of stable IDs, while rejecting duplicate identities.

Each `ParticleRenderEmitterRecord` reports `simulationPath` and
`gpuFallbackReason` for that emitter at publication. The provider uses the same
decision to route births and publish the record. `ParticleGpuFallbackReasonName`
returns a stable code such as `local-plane-collision` or `trail-force-field`.
`none` identifies GPU routing;
`cpu-policy` identifies deliberate CPU simulation, and `gpu-consumer-unavailable`
means that no GPU emitter consumer is attached. The first applicable restriction
is reported. A single effect can have both CPU and GPU emitters.

These fields describe routing rather than a GPU timing measurement. Retained
particles can complete on their previous path after a live edit. The instance's
`ParticleRuntimeQueryResult::executionPath` describes its selected backend policy;
use the per-emitter records to inspect actual routing.
The current GPU command layout requires engine module ABI 9. Rebuild providers with
the current SDK; the host rejects earlier module versions before creation.

`kb_cli run --project <dir> --report particles` runs the normal scene lifecycle
and writes `21kb.particle-execution/v1` JSON to stdout, with logs on stderr.
MCP `run_scene` accepts the same `report: "particles"` selector. The report
contains exact decimal-string identities, each emitter's route and reason,
and CPU render record counts and payload bytes from the retained snapshot
captured before shutdown. Missing providers and snapshots are explicit.
`runtime_update_wall_ns` is a decimal-string sum of wall time spent in scene
updates. It is not CPU time or per-emitter time. Unmeasured per-emitter CPU
and GPU times are `null`. This headless command does not create a GPU consumer.

Run `kb_21kb_particle_cpu_backend_tests` for collision, field and routing
regressions. On Windows, `kb_renderer_tests particle-contact-death` (CTest
`kb_renderer_particle_contact_death`) loads an authored effect through the
particle provider and compares hidden bgfx D3D11 readback against explicit
CPU simulation and a free-flight control. It requires GPU routing with no
fallback and zero CPU particles for the GPU case. Its image tolerance is two
byte values per channel, with larger differences limited to one percent of
the image for fixed-step contact/birth phase differences at particle edges.
Separate billboard and mesh readbacks check visible births, persistent death,
bounce controls, reuse of a slot with the same timestamp, and removal of the
plane. The particle editor exposes **Kill on contact** with Undo/Redo.
The existing source and compiled cache formats preserve the flag; this change
does not require an asset migration. This check also runs in the renderer's
particle mesh suite. This standalone billboard/mesh contact selector establishes
D3D11 execution; the motion suite below also checks source contact death on
additional bgfx backends.

`kb_renderer_tests particle-motion` (CTest `kb_renderer_particle_motion`) also
reads the actual GPU-written instance buffer through an RGBA32F compute image
and bgfx texture readback. It compares position and velocity with the CPU
provider after 60 fixed steps. Cases include two modules of each field type,
mixed order with wind and drag, the 16-operation limit, disabled modules,
removal of the first field, reversal of mixed order, zero-strength fields with
interleaved acceleration/drag, and transformed local emitters. The numerical
tolerances are 0.001 m and 0.003 m/s for accumulated
single-precision noise and transcendental rounding; control trajectories must
differ by more than 0.01 in position or velocity. A CPU fallback fails the test.
It also verifies contact death in the actual render records and compares a
mixed-field/contact effect's rendered image with CPU and no-field controls.
`particle-motion-kernel` exercises the same operation cases with direct GPU
commands, independently of provider routing. Both require a real GPU host;
unavailable support fails rather than passing as skipped. Append
`--backend d3d11`, `--backend d3d12`, `--backend vulkan` or `--backend opengl`
to select the actual bgfx backend. The command requires that backend in both
its numerical host and every CPU/GPU/control pixel host. The default CTest
selection remains D3D11. Unsupported names or malformed options fail before
GPU initialization.

The full `particle-motion` selector passed on Windows x64 Release and Debug with
AddressSanitizer on all four backends. Each run includes 14 world-space cases
and four transformed local
cases, native buffer capture cross-checked with compute/texture readback,
contact death, resource rejection/retry, missing depth and mixed-motion/contact
billboard pixel comparison. Maximum absolute errors after 60 fixed steps were:

| bgfx backend | Position error (m) | Velocity error (m/s) | RGB channel values above two-byte image tolerance |
| --- | ---: | ---: | ---: |
| D3D11 | 2.122e-05 | 4.256e-05 | 99 |
| D3D12 | 2.062e-05 | 5.144e-05 | 99 |
| Vulkan | 2.122e-05 | 4.256e-05 | 99 |
| OpenGL | 2.122e-05 | 4.363e-05 | 99 |

The existing 0.001 m / 0.003 m/s numerical tolerances and one-percent RGBA-channel
image bound were used for every backend. The no-field control differed by
10,725 channel levels in each run. These are source-runtime billboard results;
mesh-specific contact pixels and packaged execution remain verified on D3D11.
The performance measurements below also remain D3D11-specific.

Births use a CPU-writable bgfx upload buffer. GPU buffer copies update only the
dirty birth-ring ranges and reset their state before rebase and simulation.
Compute-write buffers are created by size and never initialized or updated from
the CPU, as required by bgfx. Unsorted billboard emitters request 144 bytes per
capacity slot without state, or 208 bytes with state, including upload and reset
buffers. Sort and trail storage add to that amount. These are logical requests,
not physical residency. Readback tests cover initial empty slots, partial and
wrapped uploads, repeated uploads before dispatch, origin changes, clear, and
rebirth with the same timestamp after contact death.

Renderer and renderer-enabled CLI validation executables install a fatal bx
assertion handler. A dependency assertion fails the test instead of continuing
with an apparently successful result. The Windows `kb_renderer_assertion_errors`
and `kb_cli_assertion_errors` tests deliberately trigger the handler and verify
failure diagnostics without a visible dialog. The four-backend runs above had
no dependency assertions, sanitizer errors or bgfx allocator leak reports.

`kb_renderer_tests particle-visual-counters` checks the renderer's visual compute
stage with native counter and instance-buffer readback. It requires the actual
backend selected with `--backend d3d11|d3d12|vulkan|opengl`. The count of selected
records must reset between dispatches, while a repeated fixed step reuses its
completed result. Mask changes, scene release, reuse and shutdown are checked.
Counter reset copies an immutable 16-byte zero record on the GPU; the CPU never
updates the compute-write counter buffer. This selector passed on all four
Windows backends in Release and Debug with AddressSanitizer, without dependency
assertions, sanitizer errors or allocator leak reports.

`Renderer::GpuParticleDraws(scene)` exposes the last dispatched GPU instance
buffers with their instance/emitter keys and requested buffer bytes. Instance
counts are capacities, including empty slots; byte counts include birth,
state, upload/reset, instance and sort buffers rather than physical GPU residency.
`GpuEmitterAllocatedBytes()` totals those allocations across this renderer's
scenes. Neither query copies particle data to the CPU during normal frames.
Draw and parameter references remain valid until the next emitter sync,
dispatch or scene release. Emitter release and accepted buffer replacement
retire the affected cached draw before mesh collection. Rejected resource
updates preserve the previous complete draw and its resources.

`ParticleGpuReadback` provides an explicit terminal diagnostic capture after
ending the last frame and before renderer shutdown. It uses bgfx buffer
readback and advances bgfx frames to finish the transfer, without advancing
simulation. Keep it outside measurement windows. Captures are bounded to
4096 instances of 80 bytes each. The raw record layout depends on the authored
output type. Invalid requests and nonfinite or unwritten GPU data fail while
preserving the previous completed capture. Tests compare this native buffer
readback with the independent compute/texture readback above and exercise
normal renderer buffers, emitter identities, allocation totals and invalid
capture boundaries on D3D11.

`kb_cli vfx-profile --project <dir> --effect /Game/Particles/Effect.kbvfx`
profiles the selected source effect in an isolated runtime scene with a hidden
Windows bgfx host and a 64x64 offscreen target. It uses the project's configured
particle provider, asset loading/compiler cache and normal renderer submission.
It preserves the effect's backend policy. It does not load the startup scene.
The component seed is fixed to 1 and simulation advances at 1/60 s per update.
One admission update precedes `--warmup` updates (default 60), followed by
`--frames` measured updates (default 180). Warmup accepts 0..36000, measured
frames 1..36000, and the entire simulation loop has a two-minute wall limit.

The command emits `21kb.particle-profile/v1` JSON on stdout; errors use stderr.
`--backend` selects `d3d11` (default), `d3d12`, `vulkan` or `opengl`; initialization
must obtain that actual backend. The initial native hidden host is Windows-only;
other platforms return an error. Backend selection alone is not verification.
`--require-gpu` fails unless every emitter uses GPU simulation without fallback
or retained CPU particles, every GPU emitter exposes an actual buffer capture,
compute and particle draws are observed, and measured GPU timestamps exist.
Explicit CPU selection remains supported without that flag.

Reports contain per-emitter routing/reasons, CPU render counts and bounded CPU
position/velocity samples. GPU captures include emitter identities, capacity,
output enum, ordered operation count, contact-death flag, requested buffer bytes
and raw 20-float instance records. `--samples` accepts 1..4096, default 64, capped
by each draw's capacity. These are sampled ring slots, including dead/unborn
records, not a measured live-particle count. Output layouts are defined in
`particle_gpu_common.sh`; billboard position/size occupy floats 0..3 and
velocity occupies 8..10, while mesh translation occupies 12..14. A contact-dead
record contains twenty zeros. Captures occur after measurement and do not
advance scene simulation.

`process_cpu_ns` and `calling_thread_cpu_ns` use Windows process/thread kernel
and user CPU counters over the measured loop; they include diagnostic collection
and all work charged to that process/thread, not per-emitter attribution.
Short windows can be quantized by the OS accounting clock. `frame_wall` measures
scene update and renderer submission elapsed time, including queue waits.
`frame_gpu` measures whole bgfx GPU frames, including compute and rendering,
using positive backend timestamp intervals. Completed frame IDs must lie in the
measured submission range and are counted once; the report lists those IDs and
sample coverage. Sixteen empty renderer frames drain delayed queries outside
the measurement window; missing samples are not fabricated. Observed maximum
bgfx compute/draw counters are observations, not aggregate frame counts.

Memory snapshots cover the pre-scene process, empty host, warm effect, measured
effect, released renderer scene, renderer shutdown and provider/scene shutdown.
Windows working set, private usage and peaks describe the whole process.
Requested emitter buffer bytes are logical allocations, not physical residency.
`bgfx_gpu_memory_used_bytes` is a backend/device counter that can include other
workloads; unavailable values are null. Releasing logical resources need not
immediately reduce driver/allocator residency or process peaks.

CTest `kb_cli_particle_profile` runs the actual command on D3D11. It checks live
and contact-dead buffer records, six repeated mixed motion operations, real GPU
timing, process counters, released allocation accounting, explicit CPU policy,
strict rejection of CPU fallback, and malformed options. It fails on unavailable
GPU support. Numerical CPU/GPU parity and pixel rendering remain covered by the
separate motion/contact tests described above. Source profiling does not prove
execution from a packaged runtime.

Local reference measurements (Windows x64, Release, bgfx D3D11, 2026-10-24;
GPU implementation `0aac28018`) include valid bgfx birth uploads, the automatic
birth-phase fix and resource rejection. The measurement checkout was `21bab5793`.
They use one billboard emitter, a single burst equal to capacity, a sphere of
radius 2 m, initial speed 0.15 m/s, size 0.03 m, 100 s lifetime and additive
output without soft particles/depth testing. Six authored fields execute as
Noise (0.7, frequency 0.5), Y-axis rotation (10 degrees/s), Attraction
(point 1,1,0; strength 0.3; radius 4), Noise (1.3, frequency 2), X-axis rotation
(15 degrees/s), Attraction (point -2,0,3; strength 0.2; unlimited radius).
The contact variant adds a killing Y-plane at -1.5 m. Each cell below is the
median of three independent process runs: 60 warmup plus 300 measured fixed
updates after admission. The source is identical between CPU/GPU runs except
for backend policy. These are whole isolated host/scene costs, not kernel-only
times or portable performance promises; OS accounting and GPU clock/driver
variation particularly affect the short GPU windows.

| Effect | Capacity | CPU policy: process ms/frame | CPU policy: GPU frame ms | GPU policy: process ms/frame | GPU policy: GPU frame ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Six fields | 1,000 | 0.729 | 0.011 | 0.312 | 0.035 |
| Six fields | 10,000 | 4.531 | 0.039 | 0.156 | 0.048 |
| Six fields | 60,000 | 25.000 | 1.354 | 0.312 | 0.115 |
| Six fields + contact death | 1,000 | 0.573 | 0.009 | 0.156 | 0.038 |
| Six fields + contact death | 10,000 | 2.812 | 0.026 | 0.208 | 0.046 |
| Six fields + contact death | 60,000 | 15.156 | 0.381 | 0.365 | 0.129 |

CPU-policy GPU times include rendering CPU-simulated particles. Contact deaths
reduce live work over time: the CPU reference ends with 432, 4,135 and 24,552
records respectively, rather than the initial capacities. GPU ring capacity
and requested allocation remain constant after death; its raw sample is not
a full live-count reduction. GPU timing coverage across the 36 runs is
263..300 of 300 unique measured frames.

| Effect | Capacity | CPU policy: private / working set MiB | GPU policy: private / working set MiB | GPU requested buffers MiB |
| --- | ---: | ---: | ---: | ---: |
| Six fields | 1,000 | 183.4 / 116.1 | 170.0 / 90.8 | 0.198 |
| Six fields | 10,000 | 187.6 / 121.1 | 175.6 / 92.9 | 1.984 |
| Six fields | 60,000 | 216.2 / 150.5 | 199.7 / 102.9 | 11.902 |
| Six fields + contact death | 1,000 | 194.6 / 127.6 | 167.6 / 89.5 | 0.198 |
| Six fields + contact death | 10,000 | 187.7 / 120.9 | 173.3 / 91.3 | 1.984 |
| Six fields + contact death | 60,000 | 216.5 / 151.1 | 200.5 / 103.6 | 11.902 |

GPU-policy runs retain the provider's conservative CPU fallback pool budget;
process memory therefore includes more than GPU command records. Requested GPU
buffers were 208 bytes per capacity slot in these unsorted stateful effects
and returned to zero after scene release in every GPU run. Whole-device memory
is deliberately excluded from per-effect attribution. All GPU runs required
actual GPU execution/readback and reported no rejected updates. Runs were
sequential, with no concurrent engine build, static analysis or GPU test.

For an effect with these authored values, reproduce each policy's measurement
with `kb_cli vfx-profile --project <dir> --effect /Game/Particles/Measure.kbvfx --backend d3d11 --warmup 60 --frames 300 --samples 64`, adding `--require-gpu`
for the GPU policy. Use three fresh processes per policy and capacity. Compare
GPU timestamp coverage and process counters, not frame wall time as CPU time.

A separate continuous-emission reference uses the same six fields without the
plane, 10,000 capacity slots, 6,000 births/s, a 1 s lifetime and no burst.
Three fresh processes per policy use 120 warmup and 300 measured updates.
The CPU reference has 6,000 live render records at the end of every run; GPU
routing retains zero CPU render records. At 100 births per fixed update, birth
and state-reset copies request 6,400 bytes per update, in addition to the
3,200-byte CPU upload. This exercises ring reuse and ongoing uploads inside the
measurement window, which the long-lived burst references do not.

| Policy | Process ms/frame | GPU frame ms | Private / working set MiB | Requested GPU buffers MiB |
| --- | ---: | ---: | ---: | ---: |
| CPU | 2.760 | 0.025 | 191.9 / 124.7 | 0 |
| GPU | 0.260 | 0.051 | 175.8 / 92.5 | 1.984 |

GPU timestamp coverage was 280..300 unique measured frames. All six runs had
no admission drops or rejected updates. Each GPU run required actual execution
and native readback, and returned its logical buffer allocation to zero on
release. These are whole-host costs, including drawing, rather than isolated
copy or kernel timings.

The source-profile execution/readback/timestamp smoke check also passed with
the repeated-field/contact source on Windows bgfx D3D12, Vulkan and OpenGL.
Numerical and pixel parity are verified separately by the selectable motion
suite above. The timing/memory tables and packaged execution checks below
cover D3D11.
Live state-buffer rejection and retry checks are described below.

| bgfx backend | Executed verification | Remaining scope |
| --- | --- | --- |
| D3D11 | CPU/GPU numerical parity, source and packaged buffer/pixel readback, resource rejection/retry, timing and memory | Other hosts and the complete release gate |
| D3D12, Vulkan, OpenGL | Source CPU/GPU numerical parity, native and independent readback, mixed-motion/contact billboard pixels, resource rejection/retry, CLI timestamp smoke checks | Packaged execution, mesh-specific contact pixels, representative measurements and other hosts |
| Metal, GLES | Compiled shader variants | Runtime execution and parity |
| WebGPU | Required package kernel closure represented | Shader/runtime execution and parity |

These source checks verify contact death and repeated ordered motion on four
bgfx backends. Platform release validation still has unresolved CI failures.
Shader compilation and CPU fallback do not count as GPU execution evidence.

Packaged Windows runtimes accept `--particle-diagnostics --headless --frames=N`
for bounded particle verification. This explicit diagnostic mode advances the
normal startup scene by exactly 1/60 s per update, disables presentation pacing,
requires 1..36000 frames and has a two-minute loop limit. It emits one terminal
line beginning `kb_game: particle_diagnostics=` with
`21kb.particle-runtime/v1` JSON. `--particle-require-gpu` fails on CPU fallback,
retained CPU records, missing GPU draws/compute or failed readback.
`--particle-samples=1..4096` bounds each emitter's samples (default 64); captures have
an aggregate 4096-slot GPU limit shared fairly between GPU emitters, and a
separate aggregate 4096-record CPU sample limit. Counts and actual samples are
reported separately. These options require the diagnostic mode.

The report identifies package loading, the exclusive package shader provider,
actual fixed steps, per-emitter route/reason, CPU samples, ordered GPU operation
types/vectors/strengths/parameters/seeds, contact-death flags and actual GPU
instance records. A terminal 64x64 offscreen scene render also returns RGBA8
pixels encoded as hexadecimal, without post processing, UI, shadows or editor
overlays. This extra view and readback do not update simulation. They establish
scene/particle material rendering, not the final display's post-process output.
The mode writes no diagnostic files and does not change the shipped player's
existing restrictions on project/scene selection or developer output switches.

The cooker and package validator now require the complete emitter kernel
closure for each target shader backend: emission, stateful collision/motion,
rebasing and sorting. Device capability checks still choose CPU simulation when
compute is unavailable. The developer inspection shader is not required by
packages. Older packages that omitted emitter kernels must be recooked; missing
required kernels fail package validation instead of silently changing the
effect's simulation path. Automatic GPU births also start at the beginning of
the fixed step in which CPU particles are aged and integrated. Manual/event
births retain their current-time origin, preserving their later integration.

CTest `kb_game_particle_gpu_package` authors two effects through CLI, cooks
GPU/CPU/bounce-control packages, and launches the actual hidden player from
directories containing only its executable, provider modules and package.
There are no loose assets or shader directories. On bgfx D3D11 it verifies
contact births, zero render records after death, six reordered mixed field
operations with distinct parameters, actual buffer/image readback, explicit
CPU policy, strict GPU rejection and malformed diagnostic arguments. After
60 fixed steps the measured maximum position/velocity errors were
0.000014 m / 0.000038 m/s, within the 0.001 m / 0.003 m/s rounding tolerances.
Images before and after contact differed from CPU by 38 and 40 channels above
the two-byte tolerance, below the existing one-percent image bound. The live
bounce control differed from contact death by 64 channels. An unavailable GPU,
fallback, failed capture or missing shader compiler fails this test.
It requires Python 3.10+ and the pinned shaderc target, or an explicit
`KB_COOKER_SHADERC` path when the compiler is built separately.
Other packaged backends remain unverified here.

GPU resource failures reject a complete parameter update rather than clearing
collision or motion features. The last accepted parameters and GPU records stay
active; rejected commands do not apply their transform, clear or birth payloads.
Resizing prepares the complete replacement before releasing the previous buffers,
so the logical budget must temporarily accommodate both allocations. A caller
can release resources and retry the full update. This does not select CPU or
remove the explicit CPU and device capability fallbacks.

`DisplayConfig::particleGpuBufferBudgetBytes` limits logical emitter buffers;
zero uses the engine limit, and larger values are clamped to that limit. Set it
before renderer initialization. `vfx-profile --gpu-buffer-budget <bytes>` exposes
the same bound for reproducible resource diagnostics. It measures requested
buffers, not physical VRAM or driver allocation success. The default is unchanged.

`Renderer::RejectedGpuParticleUpdates(scene)` identifies the emitter and typed
reason: invalid parameters, resource creation, state allocation or diagnostic
capacity. Rejections persist across clock-only updates until accepted parameters
or emitter release. A bounded diagnostic overflow remains an error until scene
release. Renderer submission reports `ParticleGpuUpdateRejected` errors; the
editor writes a deduplicated error to its console while keeping the viewport
active. CLI profiles and packaged particle reports expose `rejected_gpu_updates`
and cannot report successful GPU execution while any update is rejected.

The real D3D11 motion/kernel tests force a 224-byte logical budget with two
one-slot emitters. They verify native GPU buffer preservation after a rejected
stateful update and resize, a persistent typed error, release-and-retry of the
complete contact/noise update, actual contact-dead records and zero released
bytes. A separate successful resize verifies both replacement instance records.
The renderer particle mesh suite checks the typed error with emitter identity
through submission, persistence and successful retry. The CLI profiler test
requires a failed JSON report when its configured budget cannot admit the effect.
These tests exercise real bgfx allocations and readback within an engine logical
budget; they do not claim to reproduce physical device exhaustion.

The authored sort mode reaches GPU emitters as well as CPU render snapshots:

| Mode | Order |
| --- | --- |
| None | Original instance order |
| BackToFront | Descending view-space depth |
| FrontToBack | Ascending view-space depth |
| Distance | Descending distance from the camera |
| Age | Descending normalized age, quantized to 16 bits |

Additive billboards retain their instance order on both paths. Mesh particles
sort when their material is translucent. Depth sorting uses the view matrix,
including rotated and orthographic cameras, rather than radial distance.
`kb_renderer_tests particle-sort-modes` verifies the draw order with hidden
D3D11 readback, overlapping colours, a changed camera and off-axis particles
whose depth order differs from their distance order.

Snapshot channel warmup creates the bounded slot metadata without reserving
particle payloads. The first nonempty publication prepares 4 KiB for each
free slot. Larger publications grow only their selected slot, doubling its
capacity up to the existing 16 MiB limit.
Slots reuse their capacity after readers release them; the 64 MiB aggregate
payload limit and typed backpressure remain unchanged. Empty publications
and terminal snapshots need no payload. Allocation failure preserves the
latest complete revision and permits retrying the same publication.

Warm up the expected payload sizes and retained-slot count before measuring
steady-state allocations. A larger workload can allocate when it first grows
a free slot. `kb_21kb_particle_snapshot_tests` checks empty-channel allocations,
growth failure, retained readers, concurrent reads and allocation-free reuse.
The CPU simulation pool has a separate capacity budget. Warmup reserves its
bounded instance, command and event metadata, leaving particle columns empty.
Effect admission reserves every particle column for the combined authored
instance capacities, rounded up to a power of two and capped at the existing
scene limit. Reload reserves the replacement budget before changing live
instances. GPU effects keep the same conservative CPU fallback budget.
Capacity is reused after releases and reclaimed when the provider is destroyed.

Run `kb_21kb_particle_cpu_backend_tests --memory-probe` for a headless scene
measurement covering an empty provider, effect admission, four retained batches,
steady updates and unload. Windows reports working set, process commit and
their peaks; other hosts explicitly report unavailable process counters.
These counters include allocator and runtime overhead, unlike particle capacity.

Identical eligible local-space instances share the simulation of the lowest
dense instance index. Effect, seed, rate and capacity must match; parameters,
events, finite non-looping effects and GPU visual execution exclude sharing.
Grouping uses fixed-capacity scratch storage: one pass over parameter ownership
and eligible instances, followed by sorting only when multiple groups exist.
It therefore avoids comparing every pair of instances and does not allocate.
Pausing, releasing or compacting instances rebuilds group leadership each step.

`kb_21kb_particle_cpu_backend_tests --sharing-benchmark` measures warmed fixed
steps at 32, 64, 128 and 256 instances with identical and distinct seeds. It
reports wall-time p50/p95/p99 across 31 batches of 64 steps and requires zero
allocations. The zero-emission fixture isolates instance bookkeeping from
particle integration; it is not a rendered-scene performance claim. The regular
suite separately checks shifted particle output, changing seeds and parameters,
paused leaders and dense-slot compaction.

Both `vfx-profile` and the packaged player's `--particle-diagnostics` report
CPU capacity, spawn-budget and event-budget rejections, plus their dropped total
per emitter. These counters describe the last fixed step, not the entire run;
GPU update rejection records remain separate. `configured_cpu_particle_limit`
is explicitly labelled as a capacity ceiling, never measured throughput.
The CLI and cooked-player integration tests fill a two-particle emitter with
ten births per step and require two live render records and ten rejected births
once full, independently of the scene's much larger configured ceiling.

## External effect events

`EmitEffectAsset` queues independent effect instances at the event's world position,
with the source owner's rotation and scale. The target's emitter offsets still apply.
Requests are dispatched after the current simulation pass; events produced while a
child prewarms wait for a later pass. Dependencies resolve through the runtime asset
manager, so source files are not required by a mounted or already loaded effect.

Binding depth continues across effect boundaries. Dependency cycles are rejected;
the binding budget, fixed-step spawn budget, bounded request queue and scene instance
limit also apply. `StepTelemetry::spawnedEffects` and `rejectedEffects` report child
instance admissions separately from particle births.

Children keep the original scene owner but do not follow its transform. Releasing
the source instance leaves admitted children running. Finite effects release their
instances after their particles and queued events finish; looping effects remain
owned by the scene and drain when their owner dies. The headless editor test
`kb_21kb_particle_editor_tests --external-effects` exercises the provider, preview
snapshot, lifecycle, depth and binding budget, dependency cycles and baked cache.

## Appearance curve sampling accuracy

CPU particles evaluate the authored size and alpha curves (including easing) and
color gradient at normalized age. GPU appearance uses eight uniform samples,
including ages zero and one, with linear interpolation and clamped age. Source
save/reload and the compiled bake cache preserve all keys, easing and gradient
stops; the LUT is derived data and does not replace the authored curves.

The table is an approximation. A peak narrower than its sample spacing can be
missed completely. For piecewise linear curves/gradient channels, the exact
maximum absolute interpolation error occurs at the union of authored knots and
LUT sample ages. For a unit quadratic curve, the bound is `1 / (4 * (N-1)^2)`.
Eased curves, HDR intensity and multiplied gradient/alpha values need their own
error analysis; no universal small error or monotonic improvement is promised.

The focused headless test `kb_21kb_particle_editor_tests --curve-sampling` checks
the actual provider's eight-sample GPU commands, CPU lifetime and editor snapshots,
source persistence and cooked-cache keys/values. It compares reference tables of
4, 8, 16 and 32 samples (alternative resolutions are test references, not runtime
settings), including knot ages and their immediate float neighbors:

| Unit-amplitude fixture | 4 samples | 8 samples | 16 samples | 32 samples |
| --- | ---: | ---: | ---: | ---: |
| Linear curve/gradient | 0 | 0 | 0 | 0 |
| Quadratic curve | 0.027778 | 0.005103 | 0.001112 | 0.000261 |
| Triangular peak, ages 0.499/0.500/0.501 | 1 | 1 | 1 | 1 |
| Ramp from zero at 0.499 to one at 0.501 | 0.497 | 0.493 | 0.485 | 0.469 |

Entries are maximum absolute errors, with `0.00002` test tolerance for float
arithmetic. The interpolation comparison executes the shader's sampling arithmetic
on the host; this test does not assert device readback or bitwise GPU agreement.
