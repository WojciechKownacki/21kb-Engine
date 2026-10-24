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
CPU events and frame-pointer call stacks. Its test-results artifact includes the
workload log, aggregated symbol costs and call graph. Setup and teardown are also
sampled; use the `RunSceneUIBuildFrameProfile` / `SceneUIQueries::BuildFrame` call
chains to distinguish frame work from scene construction. The diagnostic adds no
production instrumentation and leaves the original failing test result unchanged.
Raw recordings and stack memory are not uploaded. If sampling is unavailable, the
diagnostic step fails visibly; no profiling or performance acceptance is inferred.
