# Particle simulation

The particle provider selects simulation per emitter. `GpuVisualPreferred`
allows CPU simulation when an emitter needs behavior the GPU kernel cannot
preserve. CPU particles still use the renderer's particle draw path.

Collision planes with `killOnContact` use CPU simulation: particles end their
lifetime at contact and disappear from subsequent render snapshots. Bouncing
planes can use GPU simulation. An authored plane never implicitly enables
screen-depth collision; the renderer's explicit depth-collision parameter is
independent of the plane.

The GPU parameter block represents one Noise, AxisRotation and Attraction
module each. Emitters with multiple enabled modules of any of these types use
CPU simulation, preserving every module and its authored order. Disabled
modules do not affect eligibility.

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
