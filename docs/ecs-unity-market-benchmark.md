# ECS market benchmark: 21kb vs Unity

This document defines the benchmark protocol for comparing 21kb ECS against
Unity Entities on the same machine.

## 21kb run

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tests/run-ecs-market-benchmark.ps1 `
  -BuildDir build/ecs-benchmarks-release `
  -Config Release `
  -Scales 10000,100000,1000000,10000000,100000000 `
  -Frames 5 `
  -Warmup 1
```

Outputs:

- `Saved/Benchmarks/EcsMarket/21kb_ecs_<scale>.json`
- `Saved/Benchmarks/EcsMarket/21kb_ecs_market_summary.csv`
- `Saved/Benchmarks/EcsMarket/21kb_ecs_market_manifest.json`

The script skips scales that exceed the memory guard. Use `-Force` only on a
machine where swapping or OOM is acceptable.

The runner passes `--market-scale` to `kb_ecs_benchmarks`, so each scale measures
only the dense entity workloads that map cleanly to Unity Entities jobs:

- `position_velocity_batch_read`
- `position_velocity_linear_update`
- `transform_local_to_world_no_hierarchy`

## Unity baseline protocol

Use a standalone Development Build disabled, Burst enabled, Jobs enabled,
VSync disabled, and the same physical machine. Run one build per Unity version
and Entities package version you want to compare.

Recommended scenes/jobs:

1. `position_velocity_batch_read`
   - Create `N` entities with `Position` and `Velocity`.
   - Sum both components in an `IJobEntity` or `IJobChunk` read-only job.

2. `position_velocity_linear_update`
   - Create `N` entities with `Position` and `Velocity`.
   - Update position from velocity in an `IJobEntity` or `IJobChunk` job.

3. `transform_local_to_world_no_hierarchy`
   - Create `N` entities with local/world transform-like components.
   - Write world transform from local transform with no parent hierarchy.

4. `structural_changes_command_buffer`
   - Per frame: destroy `N/40`, create `N/40`, add marker to `N/40`,
     remove marker from `N/40`.
   - Use `EntityCommandBuffer` playback once per frame.

5. `prefab_spawn_registered`
   - Bake a prefab/entity prototype with 1, 4, and 16 nodes.
   - Instantiate 10k, 50k, and 100k instances.

CSV columns should match:

```text
engine,scale,benchmark,dataset,entities,frames,warmup_frames,time_ms_min,time_ms_avg,time_ms_p95,throughput_entities_per_second,cpu,thread_count,build_config,commit
```

## Interpretation rules

- Compare Release/Player builds only.
- Compare the same scale and benchmark name.
- Report skipped 10M/100M runs explicitly; do not extrapolate them as measured.
- Keep memory pressure visible. A benchmark that only wins by swapping is a fail.
- Prefer `time_ms_avg`, `time_ms_p95`, and `throughput_entities_per_second`.
- Do not compare editor mode Unity results against 21kb native Release results.
