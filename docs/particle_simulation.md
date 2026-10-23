# Particle simulation

The particle provider selects simulation per emitter. `GpuVisualPreferred`
allows CPU simulation when an emitter needs behavior the GPU kernel cannot
preserve. CPU particles still use the renderer's particle draw path.

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

Collision planes with `killOnContact` use CPU simulation: particles end their
lifetime at contact and disappear from subsequent render snapshots. Bouncing
planes can use GPU simulation. An authored plane never implicitly enables
screen-depth collision; the renderer's explicit depth-collision parameter is
independent of the plane.

The GPU parameter block represents one Noise, AxisRotation and Attraction
module each. Emitters with multiple enabled modules of any of these types use
CPU simulation, preserving every module and its authored order. Disabled
modules do not affect eligibility.

Each `ParticleRenderEmitterRecord` reports `simulationPath` and
`gpuFallbackReason` for that emitter at publication. The provider uses the same
decision to route births and publish the record. `ParticleGpuFallbackReasonName`
returns a stable code such as `kill-on-contact`, `repeated-noise`,
`repeated-axis-rotation` or `repeated-attraction`. `none` identifies GPU routing;
`cpu-policy` identifies deliberate CPU simulation, and `gpu-consumer-unavailable`
means that no GPU emitter consumer is attached. The first applicable restriction
is reported. A single effect can have both CPU and GPU emitters.

These fields describe routing rather than a GPU timing measurement. Retained
particles can complete on their previous path after a live edit. The instance's
`ParticleRuntimeQueryResult::executionPath` describes its selected backend policy;
use the per-emitter records to inspect actual routing.
The updated record layout requires engine module ABI 6. Rebuild providers with
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
regressions. On Windows, `kb_renderer_tests particle-collision-fallback`
loads an authored effect through the particle provider and compares hidden
D3D11 readback against CPU simulation and a free-flight control. This check
also runs in the renderer's particle mesh suite.

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
