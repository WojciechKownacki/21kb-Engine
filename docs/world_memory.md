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

`measured_bytes` is the sum of the kinds that have a reading. `error_ratio` is `measured_bytes / estimated_resident_bytes` for the cells
resident now: above 1 the budget charges too little, below 1 too much. The runtime takes this ratio each time the set of resident cells
settles (nothing loading or unloading) and keeps the lowest and highest as `minimum_error_ratio` and `maximum_error_ratio`, with the number
of `samples`.

Measured with the engine's own test worlds (`world_memory` line of `kb_engine_tests`, and `kb_cli_tests`), which are small cells of a few
nodes: the ratio of instances plus assets to the estimate was 0.5 for a 3 by 3 neighbourhood of two-node cells and 1.9 for a single cell.
The spread is the point: the estimate is a rule of thumb, accurate to within a factor of two for these cells, and the ratio depends on the
content. A real project should read its own: run the scene with a stream focus and look at `error_ratio`, `minimum_error_ratio` and
`maximum_error_ratio` after the player has moved through the world.

## Using it

```
kb_cli run --project <dir> --frames 600 --report world-memory
```

prints `21kb.world-memory/v1` JSON on stdout (logs go to stderr): the budget, the estimate for the resident cells, the number of resident
units, `measured_bytes`, the ratios and a `categories` array with `kind`, `source`, `bytes`, `baseline_bytes` and a note for each kind.

## Limits

- Graphics and physics memory are not read by any shipped host yet; `kb_cli run` and the player report them unavailable. The report takes a
  probe so a host that has them (bgfx's texture and render target memory, the physics allocator) can fill them in.
- Asset memory is estimated from file sizes. A packaged game without loose files has no file to measure, so its assets read zero; a loader
  that reports its payload size would fix both.
- The ratio compares the budget's one number with a sum of different things; it says whether the budget is realistic, not which kind is
  wrong. The kinds say that.
- Nothing here changes the budget or recalibrates the 512 bytes per entity: the measurements above are too few and too small to set a
  constant from.
