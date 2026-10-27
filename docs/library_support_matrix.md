# Engine Library support matrix

This matrix describes the configurations CI builds and tests. Every row below is one
job of `.github/workflows/ci.yml`; `scripts/tests/test_support_matrix.py` fails
when the two disagree.

## Build and test jobs

| CI job | Runner | Build | Scope | CI targets |
| --- | --- | --- | --- | --- |
| Windows MSVC | `windows-2025-vs2026` | Visual Studio 2026, Release, editor and renderer on, strict first-party warnings | Engine, renderer, Win32 editor, game host, CLI; Native, Lua and Visual Graph frontends | Complete default build graph: all configured executables and providers |
| Windows MSVC ASan | `windows-2025-vs2026` | Visual Studio 2026, Debug, AddressSanitizer | Engine UI and hierarchy boundaries, renderer, particle runtime, read batches and process lifetimes | `kb_engine_tests`, `kb_editor_tests`, `kb_21kb_particle_snapshot_tests`, `kb_21kb_particle_cpu_backend_tests`, `kb_21kb_particle_editor_tests`, `kb_21kb_particle_renderer_tests`, `kb_renderer_tests`, `kb_ecs_query_shutdown_tests`, `kb_background_read_tests`, `kb_test_process_error_tests` |
| Linux GCC ASan UBSan | `ubuntu-24.04` | Unix Makefiles, Debug, editor and renderer off, ASan and UBSan | Headless engine runtime; Native, Lua and Visual Graph frontends | Complete default build graph: all configured executables and providers |

The Windows MSVC job also runs the script API compatibility check
(`kb_cli api-check`) and the packaging script tests
(`python -m unittest discover -s scripts/tests`). The Windows MSVC ASan job
runs selected CTest cases from its nine targets, including UI frames, exact scalar
and batch transform comparisons, scene and ECS hierarchy limits, shader resource
lifetimes, native reads and process errors.
The sanitized jobs skip tests labelled `performance`.

`kb_engine_tests scene-transform-golden` runs the transform regression directly.
For flat, overlay, observed-component and parented scenes it compares batch writes
with ordered scalar writes, bit for bit, including versions, interpolation and
render-proxy updates before and after each frame. It also checks the recorded
golden hashes and compares transform-pass writes with batch writes. Input and
state hashes are logged separately to diagnose fixture math differences between
toolchains. Each recorded state corpus is keyed by its exact mover-input hash: the
verified MSVC and GNU system math inputs have separate records. Unknown input
hashes fail, as does any state difference from its matching record. The scalar
and transform-pass comparisons remain bit-exact on every toolchain.

## Other jobs

| CI job | Runner | What it runs |
| --- | --- | --- |
| Web player bootstrap | `ubuntu-24.04` | Strict Release build of the production WebGL2 host; real browser HTTP, binary and interrupted-body failures before native startup; no GPU rendering acceptance |
| Web input ASan UBSan | `ubuntu-24.04` | Production touch collector compiled to WebAssembly with ASan and UBSan; trusted headless Chromium touch events and native state readback |
| Linux native read batches | `ubuntu-24.04` | Isolated background read batch tests under ASan and UBSan, covering priority, cancellation and exact ranges |
| clang-tidy gate | `windows-2025-vs2026` | `scripts/clang_tidy_gate.py` over changed first-party sources; every source when a first-party header changes, on manual dispatch, or in the weekly scheduled run |
| Fuzz untrusted-input readers | `windows-2025-vs2026` | The `kb_fuzzers` libFuzzer harnesses with AddressSanitizer: corpus replay, then 60 seconds of fresh inputs per reader |

The Web player job uses up to four compile jobs within its 20-minute budget,
matching the [standard public Ubuntu runner's CPU capacity](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
Its pinned toolchain uses a 1 GiB compiler cache across commits. Separate restore
and save steps attempt to preserve completed compiler objects after a failed or
interrupted build, and cache statistics are reported independently of build
success. A skipped bootstrap test or interrupted build does not qualify the
browser host.

The static-analysis gate rejects an empty compilation database or source filter
selection with exit code 2. Check `compile_commands.json`, `--filter` and
`--filter-file` when no first-party sources are selected. An unchanged
`--changed-since` scope explicitly reports `skipped`; it does not report an
analysis pass or a finding count. Invalid filter expressions also return a CLI
diagnostic with exit code 2.

## Not covered by CI

There is no macOS job: macOS is not a tested platform. Native renderer, editor
and packaged desktop host execution is covered only on Windows; the Linux
native row builds neither the renderer nor the editor. The Web player job
cross-compiles the production WebGL2 host and verifies failures before native
startup. Its empty package sentinel must never be requested; it does not cook
or run a complete game and does not qualify a GPU backend. The isolated Web
input job also does not validate browser rendering or asset packs. Android and
WebGPU players are not built in CI. Unvalidated runtime cells remain unsupported
until their actual platform workflows pass.

`kb_engine_library_tests` verifies the catalog and parity of Native, Lua and
Visual Graph entry points; it runs in the Windows MSVC and Linux rows.

Full jobs build the complete default graph, so newly registered test executables
and their providers do not need a second target list. Test results are retained
as JUnit XML and the CTest log, including failures and skips.

When adding a platform, rendering backend, or frontend, update this table and
the matching CI configuration in the same change. A claimed production cell must
name a test that actually executes on that row.
