# What a streamed world costs: the memory report

The streaming budget of a partitioned world (`WorldStreamingBudget::maxResidentBytes`) charges every cell with one number, its
`estimatedBytes`: the size of its serialized scene plus a fixed 512 bytes per entity it creates. That is an estimate of everything a
cell costs in memory, and until now nothing said how good it is. `WorldPartitionRuntime::MemoryReport()` and
`kb_cli run --report world-memory` split what the cells actually hold by kind and compare it with what the budget charged.

## The kinds

| Kind | Source here | Meaning |
|---|---|---|
| `assets_cpu` | estimated | the file sizes of the assets that are loaded (a decoded texture or mesh is larger; only its loader knows by how much) |
| `instances` | measured | the scene's ECS storage in use (entities and components), from the ECS telemetry |
| `scripts` | measured by a host probe | the script runtime's heap; `kb_cli run` reads the Lua allocator |
| `physics` | unavailable unless a host sets a probe | the physics world |
| `gpu` | unavailable unless a host sets a probe | graphics resources |

Each number is the growth since the first world was placed, so memory the scene already held (the camera, the UI) is not charged to the
world. A kind with no reading is reported `unavailable` with a null size and `has_unavailable_kinds` true; it is never guessed and never
counted as zero in the total. A host that can read a kind sets a probe before the world is placed:
`WorldPartitionRuntime::SetMemoryProbe(kind, [] { return bytes; })`.

## The measured error

`measured_bytes` sums only categories whose source is `measured`. `estimated_bytes` sums categories whose source is `estimated`;
`has_estimated_kinds` identifies that partial coverage even when their growth is zero. Neither total includes unavailable categories.
`estimated_resident_bytes` remains the separate cell budget charge.

`error_ratio` is `measured_bytes / estimated_resident_bytes` for the cells resident now. When any category is estimated or unavailable,
this compares only measured growth with the budget; a ratio below 1 does not prove that the budget overestimates total memory.
The runtime samples this ratio when the resident cell count settles and changes, retaining its minimum, maximum and sample count.
A complete estimation error requires measurements covering all five categories. File-size estimates and synthetic host probes do not
provide that calibration.

## Using it

```
kb_cli run --project <dir> --frames 600 --report world-memory
```

prints `21kb.world-memory/v1` JSON on stdout (logs go to stderr): the budget, the estimate for the resident cells, the number of resident
units, `measured_bytes`, `estimated_bytes`, coverage flags, the ratios and a `categories` array with `kind`, `source`, `bytes`, `baseline_bytes` and a note for each kind.

## Limits

- Graphics and physics memory are not read by any shipped host yet; `kb_cli run` and the player report them unavailable. The report takes a
  probe so a host that has them (bgfx's texture and render target memory, the physics allocator) can fill them in.
- Asset memory is estimated from file sizes. If any loaded asset has no readable loose file, the category is unavailable, including in
  a packaged game without loose sources. A loader reporting its decoded payload size is required for a measurement.
- Measurements count growth above a baseline, not total process memory or allocations retained after unloading.
- Nothing here changes the budget or recalibrates the 512 bytes per entity. Complete subsystem measurements and representative workloads
  are still required to calibrate that estimate.
