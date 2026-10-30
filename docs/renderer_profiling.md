# Scene profiling and resource diagnostics

Run `python scripts/profile_scene.py --player <build>/kb_game.exe --project <project-directory> --output-dir <new-directory>`.
The hidden native player runs a fixed simulation step with uncapped presentation.
Defaults are D3D11, Deferred lighting, 960 x 540, 240 warmup frames,
600 measured frames and three independent processes. Override these explicitly
when comparing configurations. Keep scene assets, camera, animation time,
resolution, lighting and resulting image equivalent before comparing timings.

The report retains raw CSV, per-frame JSON, process logs, executable and project-file hashes,
actual backend and adapter IDs. On Windows it matches the adapter against the
OS driver version; other systems can supply `--driver-label`. Unavailable driver
information is explicit. CPU p50/p95/p99 include processing wall time, simulation,
BeginFrame plus submission, EndFrame, and scene synchronization separately.
They are overlapping scopes, so do not add them. Warmup is excluded. GPU queries
are delayed: each query from a measured bgfx frame is counted once, and missing
queries remain missing. A failed run, incomplete trace, missing resource or
dropped geometry fails the benchmark. Shadow capacity warnings remain visible
in the summary because excess lights deliberately render without shadows.

A separate fixed-step process captures `reference.png` at `--capture-frame`
(default 120). Its readback is excluded from timing. Compare this image and the
scene configuration before making performance claims. Results measure this
engine and machine only; they establish no ranking against another engine.

## Memory scope and lifetime

`Renderer::RuntimeResourceStats().memory` reports logical GPU payload estimates
for registered mesh vertex/index/terrain-weight storage and texture storage
(including mip levels, array layers and cube faces). The same values appear in
the player's `.trace.jsonl`. Live and deferred-destruction bytes are separate.
Retained bytes include both; the peak and cumulative allocated/released counters
cover the registry's lifetime. An in-place upload does not allocate another
payload. Replacement/resize counts both old and new resources until the old
resource's deferred release. Scene unload moves allocations to that queue.

These estimates exclude driver alignment, staging, shader programs and resources
owned outside the registry. bgfx texture and render-target totals are also
estimates and overlap registry data: do not sum them. bgfx GPU used/max fields
are reported separately as backend/driver values when available, otherwise null.
They can lag and can have a broader scope than the scene. None of these values
asserts that logical payload bytes equal physically resident VRAM. Per-run peaks
refer to sampled frames; the registry peak additionally retains earlier spikes.
In particular D3D11 obtains NVAPI adapter memory, which includes other applications.
Submit workload counters sum across passes/views and are not unique mesh or light counts.

## GI geometry and shadow limits

Static meshes normally retain CPU triangles for voxel GI. A mesh without that
copy falls back to its bounding box and emits `GiBoundingBoxFallback`, with
the affected mesh count in `giBoundingBoxMeshCount`. The player prints a warning
when that nonzero count changes. Prepared data can be published with
`RenderResourceRegistry::SetMeshProxyGeometry`: finite positions and valid triangle
indices, at most 200,000 triangles / 600,000 vertices. The registry copies the
data, rejects malformed input without replacing the previous proxy, and clears
it after dynamic vertex uploads. Republish updated geometry after such uploads;
two empty arrays explicitly clear it. This API does not infer skinned poses.

The point/spot/area/tube atlas selects at most 32 lights. Further candidates
remain lit without atlas shadows. `selectedShadowLightCount`,
`droppedShadowLightCount` and `ShadowLightLimit` expose that decision; selection
counts do not guarantee that every light found a shadow caster. Tile resolution
decreases at higher light counts. Atlas allocation bytes and shadow draw counts
remain available alongside these counters. Native hidden GPU tests exercise
capacity overflow, moving lights, atlas resizing and restoration of the image.
