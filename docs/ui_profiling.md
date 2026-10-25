# UI frame profiling

Run `kb_engine_tests scene-ui-profile` without opening a window. The workload uses
the same fixture as the UI performance regression: one million plain scene roots,
one canvas, and eight text widgets at 1920 by 1080. After two warmup frames it builds
10,000 frames, checks all nine elements on every frame, and verifies that the final
frame contents match the warmed frame.

The JSON lines use schema `21kb.scene-ui-profile/v1`. The completion record reports
p50, p95 and p99 elapsed wall time in milliseconds, plus total allocation counts and
bytes during the measured loop. Large allocations mean at least 16 KiB. Scene setup,
warmup, sample storage, sorting and output are outside that allocation tally. The
percentiles use the sorted sample at `floor((frameCount - 1) * percentile / 100)`.
`budgetEvaluated` is always false: this workload is a diagnostic for profilers and
does not replace or pass the separate timing and allocation regression test.

After a Linux CI `kb_engine_scene_ui` failure, the job samples this workload with `perf` software
CPU events and frame-pointer call stacks. Tool discovery follows the kernel package's
versioned links under `/usr/lib/linux-tools` and selects the latest installed executable.
Its test-results artifact includes the workload log, aggregated symbol costs and call graph.
Setup and teardown are also
sampled; use the `RunSceneUIBuildFrameProfile` / `SceneUIQueries::BuildFrame` call
chains to distinguish frame work from scene construction. The diagnostic adds no
production instrumentation and leaves the original failing test result unchanged.
Raw recordings and stack memory are not uploaded. If sampling is unavailable, the
diagnostic step fails visibly; no profiling or performance acceptance is inferred.

Frame validation counts active layout components from the same typed reads that
validate their values. It does not issue separate presence queries for those
components. Invalid values and multiple layouts still refuse the frame; existing
frame-content and timing regressions remain the acceptance checks.

Frame builds resolve each UI component type ID once and reuse it for later
entities in that build. For entities held by native storage, component reads go
through its checked entity lookup; absent components do not search the backend
mirror. Other entities retain the scene query path. The cache is rebuilt for each
frame, so component additions, removals, edits and entity generations remain
visible. It adds one fixed array of 31 component IDs and no heap allocations.

An x64 MSVC measurement of the workload above gave these wall-time percentiles
in milliseconds, before and after reusing native component lookups:

| Configuration | Before p50 / p95 / p99 | After p50 / p95 / p99 |
| --- | --- | --- |
| Release | 0.0234 / 0.0242 / 0.0257 | 0.0108 / 0.0110 / 0.0114 |
| Debug ASan | 1.2843 / 1.5452 / 2.2792 | 0.9513 / 1.2092 / 1.8693 |

Allocation counts and bytes were unchanged, with no allocations of 16 KiB or
more. The complete `scene-ui` suite also passed in both configurations, including
the recorded frame hash, component lifetimes and existing timing budgets. These
local measurements do not qualify Linux timing or packaged browser performance;
the original CI regression must still pass on its own runner.
