# What a streamed world costs: the memory report

The streaming budget of a partitioned world (`WorldStreamingBudget::maxResidentBytes`) charges every cell with one number, its
`estimatedBytes`: the size of its serialized scene plus a fixed 512 bytes per entity it creates. This charge covers cell admission;
it does not separately charge HLOD proxies, referenced asset payloads, shared backend allocations or graphics resources. It is not a
cap on total world or process memory. `WorldPartitionRuntime::MemoryReport()` and `kb_cli run --report world-memory` report subsystem
growth separately from that charge, making the estimator's missing costs visible.

## The kinds

| Kind | Source here | Meaning |
|---|---|---|
| `assets_cpu` | measured with complete loader coverage; otherwise estimated or unavailable | decoded payload objects and owned buffer capacities; incomplete coverage retains an explicitly labeled file-size estimate when readable |
| `instances` | measured | the scene's ECS storage in use (entities and components), from the ECS telemetry |
| `scripts` | measured by a host probe | the script runtime's heap; `kb_cli run` and the game host read the Lua allocator |
| `physics` | measured when the backend supports allocator telemetry | resident allocator requests across the physics backend, including shared tables and other worlds |
| `gpu` | game host on D3D11 when supported by the driver; otherwise unavailable without a probe | process usage on the active graphics adapter, including local and shared memory |

Each number is the growth since the first world was placed, so memory the scene already held (the camera, the UI) is not charged to the
world. A kind with no reading is reported `unavailable` with a null size and `has_unavailable_kinds` true; it is never guessed and never
counted as zero in the total. A host that can read a kind sets a probe before the world is placed:
`WorldPartitionRuntime::SetMemoryProbe(kind, [] { return bytes; })`.

CPU asset rows also carry `known_payload_bytes`, `unmeasured_payloads` and `baseline_unmeasured_payloads`. These expose decoded-memory
coverage even when `bytes` remains an estimate or is unavailable. A complete decoded reading requires both baseline and current coverage
to be complete. Payloads held by external handles remain in the measurement after removal from the asset cache.

## The measured error

`measured_bytes` sums only categories whose source is `measured`. `estimated_bytes` sums categories whose source is `estimated`;
`has_estimated_kinds` identifies that partial coverage even when their growth is zero. Neither total includes unavailable categories.
`estimated_resident_bytes` remains the separate cell budget charge.

`error_ratio` is `measured_bytes / estimated_resident_bytes` for the cells resident now. When any category is estimated or unavailable,
this compares only measured growth with the budget; a ratio below 1 does not prove that the budget overestimates total memory.
Each requested report samples this ratio when no cell load or unload is pending, retaining its minimum, maximum and sample count.
This includes late asset and host allocations even when the resident cell count stays fixed. Shared-process growth can include unrelated
allocations; complete counter coverage alone does not establish attribution to streamed cells.
A complete estimation error requires measurements covering all five categories. File-size estimates and synthetic host probes do not
provide that calibration.

## Using it

```
kb_cli run --project <dir> --frames 600 --report world-memory
```

prints `21kb.world-memory/v1` JSON on stdout (logs go to stderr): the budget, the estimate for the resident cells, the number of resident
units, `measured_bytes`, `estimated_bytes`, coverage flags, the ratios and a `categories` array with `kind`, `source`, `bytes`, `baseline_bytes` and a note for each kind.

## Limits

- The game host installs a graphics probe using `ProcessGpuMemoryBytes(renderer)`. It reads the driver's process usage on D3D11;
  other backends, unsupported drivers and an inactive renderer return unavailable. The scope includes other graphics allocations in the
  process, not just streamed meshes and textures. Physics is unavailable when its backend has no allocator telemetry; it is never guessed.
- Texture, mesh, Lua source/metadata and world cell index loaders report decoded CPU capacities. Uninstrumented asset types remain explicit in the coverage counts. With incomplete
  coverage and missing loose files, the full asset category is unavailable; the known decoded portion remains visible separately.
- CPU payload readings exclude allocator padding, shared ownership bookkeeping, loader caches and decoding work not yet published. Physics
  readings exclude allocator padding and the telemetry header. They describe allocation requests, not process RSS or total device memory.
- Measurements count growth above a baseline, not total process memory. Allocations retained after unloading remain in the reading until released.
- The admission formula remains serialized bytes plus 512 bytes per entity. The calibration below measures its error against subsystem
  growth; it does not turn that heuristic into a total-memory bound or justify a universal correction factor.

### Game frame profile

`kb_game --project <dir> --headless --frames 240 --profile-file <file.csv>` records world memory alongside frame timing.
The `world_budget_bytes`, `world_cell_estimate_bytes` and `world_resident_units` columns describe streaming charges.
`world_measured_bytes`, `world_estimated_bytes` and `world_error_ratio` use the same report definitions as the CLI.
Each of `cpu`, `instances`, `scripts`, `physics` and `gpu` has `world_<kind>_source`,
`world_<kind>_bytes` and `world_<kind>_baseline_bytes`: source 0 means unavailable, 1 estimated, and 2 measured.
Unavailable sizes are empty CSV fields. CPU coverage is exposed by `world_known_payload_bytes`,
`world_unmeasured_payloads` and `world_baseline_unmeasured_payloads`.
Process-wide graphics growth can include renderer initialization; use a warmed control when calibrating streaming costs.

## Observed calibration

A headless D3D11 run at 640 by 360 streamed a six-cube world with five cells, a camera and a directional light.
The focus started outside the world, entered it, left it and entered again in the same 600-frame process. Each difference below
compares a settled loaded sample with its preceding settled unloaded sample, after renderer startup. The script controller remains
active in both samples. All five categories had readings, with no unknown CPU payloads or missing rendering resources.

| Reading (bytes) | First visible load | Repeated load |
|---|---:|---:|
| Cell admission estimate | 5,691 | 5,691 |
| Decoded asset CPU growth | 3,788 | 3,788 |
| ECS instance growth | 2,244 | 2,244 |
| Script heap growth | 14,721 | 11,816 |
| Physics allocation growth | 1,440 | 1,440 |
| Process GPU growth | 23,326,720 | 65,536 |
| Total observed growth | 23,348,913 | 84,824 |

The observed totals are approximately 4,102.8 and 14.9 times the cell admission estimate. These are measured errors for this
workload and counter scope, not bounds for other worlds. The first visible load can allocate shared rendering resources even after
empty-frame warmup; subsequent loads can reuse them. The script heap also includes the controller's work. A fixed per-entity multiplier
cannot attribute those shared costs to cells. Keep the admission estimate separate from subsystem readings, and calibrate the actual
content, host and load/reuse sequence before treating a streaming budget as a memory limit.
