# Engine Library support matrix

This matrix describes support only where CI executes the named target. A cell
without a target is intentionally not a support claim. Every row below is one
job of `.github/workflows/ci.yml`; `scripts/tests/test_support_matrix.py` fails
when the two disagree.

## Build and test jobs

| CI job | Runner | Build | Scope | CI targets |
| --- | --- | --- | --- | --- |
| Windows MSVC | `windows-2025-vs2026` | Visual Studio 2026, Debug, editor and renderer on, strict first-party warnings | Engine, renderer, Win32 editor, game host, CLI; Native, Lua and Visual Graph frontends | `kb_21kb_particle_asset_tests`, `kb_21kb_particle_snapshot_tests`, `kb_21kb_particle_stage2_tests`, `kb_21kb_particle_cpu_backend_tests`, `kb_21kb_particle_editor_tests`, `kb_engine_tests`, `kb_audio_runtime_tests`, `kb_engine_library_tests`, `kb_library_benchmarks`, `kb_library_safety_tests`, `kb_ecs_api_tests`, `kb_ecs_scheduler_correctness_tests`, `kb_ecs_deterministic_replay_tests`, `kb_ecs_stress_tests`, `kb_ecs_snapshot_scale_tests`, `kb_renderer_tests`, `kb_21kb_particle_renderer_tests`, `kb_skeletal_animator_benchmarks`, `kb_terrain_editor_tests`, `kb_editor_particle_authoring_tests`, `kb_editor_particle_bake_host_tests`, `kb_editor_tests`, `kb_editor_material_graph_canvas_tests`, `kb_game_core_tests`, `kb_game_tests`, `kb_editor_build_game_tests`, `kb_crash_reporting_tests`, `kb_standalone_player`, `kb_editor`, `kb_cli`, `kb_cli_tests` |
| Windows MSVC ASan | `windows-2025-vs2026` | Visual Studio 2026, Debug, AddressSanitizer | Renderer and particle runtime under ASan | `kb_21kb_particle_snapshot_tests`, `kb_21kb_particle_cpu_backend_tests`, `kb_21kb_particle_editor_tests`, `kb_21kb_particle_renderer_tests`, `kb_renderer_tests` |
| Linux GCC ASan UBSan | `ubuntu-24.04` | Unix Makefiles, Debug, editor and renderer off, ASan and UBSan | Headless engine runtime; Native, Lua and Visual Graph frontends | `kb_21kb_particle_asset_tests`, `kb_21kb_particle_snapshot_tests`, `kb_21kb_particle_stage2_tests`, `kb_21kb_particle_cpu_backend_tests`, `kb_engine_tests`, `kb_audio_runtime_tests`, `kb_engine_library_tests`, `kb_library_benchmarks`, `kb_library_safety_tests`, `kb_ecs_api_tests`, `kb_ecs_scheduler_correctness_tests`, `kb_ecs_deterministic_replay_tests`, `kb_ecs_stress_tests`, `kb_ecs_snapshot_scale_tests`, `kb_cli_tests` |

The Windows MSVC job also runs the script API compatibility check
(`kb_cli api-check`) and the packaging script tests
(`python -m unittest discover -s scripts/tests`). The Windows MSVC ASan job
runs only the tests of its five targets; the sanitized jobs skip tests labelled
`performance`.

## Other jobs

| CI job | Runner | What it runs |
| --- | --- | --- |
| clang-tidy gate | `windows-2025-vs2026` | `scripts/clang_tidy_gate.py` over the changed first-party sources, and over every source in the weekly scheduled run |
| Fuzz untrusted-input readers | `windows-2025-vs2026` | The `kb_fuzzers` libFuzzer harnesses with AddressSanitizer: corpus replay, then 60 seconds of fresh inputs per reader |

## Not covered by CI

There is no macOS job: macOS is not a tested platform. The renderer, the
editor and the packaged game hosts build only on Windows in CI; the Linux row
builds neither the renderer nor the editor, and no CI job builds Android or
the Web targets. None of these may be described as supported until a CI job
builds and tests them.

`kb_engine_library_tests` verifies the catalog and parity of Native, Lua and
Visual Graph entry points; it runs in the Windows MSVC and Linux rows.

When adding a platform, rendering backend, or frontend, update this table and
the matching CI target list in the same change. A claimed production cell must
name a test that actually executes on that row.
