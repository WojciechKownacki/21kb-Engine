# BASE-RENDER-001 — Reconcile Renderer Foundations

| Field | Value |
|---|---|
| Status | `READY` |
| Sprint | `S00` |
| Priority | `P0` |
| Depends on | None |
| Blocks | `BASE-INTEGRATE-001` |
| Change type | Read-only renderer audit with one documentation artifact |
| Runtime budget impact | None; this item must not change renderer code |
| Platform coverage | Current Win32/bgfx path and repository evidence for other backends |
| Required gates | Correctness, build quality, performance, concurrency, diagnostics, release |

## Goal

Produce an evidence-backed baseline of the renderer, render graph, resource ownership, scene extraction, shaders, materials, post-processing, GPU-driven paths, editor presentation, capability reporting, and GPU verification infrastructure.

## Repository context

The renderer already contains bgfx initialization, render-graph validation, frame-pipeline tests, resource and material tests, GPU readback coverage, shader tooling, runtime statistics, and Windows smoke infrastructure. The capability inventory includes both high-level renderer requests and later detailed engineering tasks. This audit must identify the canonical implementation and the real gaps without treating either list as authoritative code state.

Expected read scope:

- `README.md`
- `CMakeLists.txt`
- `CMake/KbBgfxRuntimeShaders.cmake`
- `sources/renderer/`
- `sources/renderer/tests/`
- renderer-facing code under `sources/editor/` and `sources/engine/`
- `tests/run-render-smoke.ps1`
- `tests/run-material-graph-release-gate.ps1`
- renderer entries in `Sprint/roadmap/CapabilityInventory.md`

Exclusive source-controlled write scope:

- `Sprint/roadmap/baseline/renderer.md`
- `Sprint/roadmap/evidence/BASE-RENDER-001/`

## Deliverables

Create `Sprint/roadmap/baseline/renderer.md` containing:

1. configured renderer targets, supported and compiled bgfx backends, host restrictions, and shader profiles;
2. canonical ownership of device/context, resources, views, render graph, scene extraction, submission, shaders, materials, and editor viewports;
3. a pass and resource-flow summary grounded in current code;
4. existing CPU tests, GPU tests, smoke commands, debug reports, captures, and performance counters;
5. explicit distinction between implemented GPU execution, CPU validation, fallback paths, and enumerated future features;
6. inventory entries classified as implemented, partial, duplicate, backend-incompatible, architecture-decision-dependent, or absent;
7. unresolved constraints around bgfx, explicit barriers and aliasing, native backend access, ray tracing, bindless resources, HDR, and device loss;
8. proposed bounded work-item titles for genuine gaps, without promotion.

## Non-goals

- Do not change renderer, shader, editor, test, build, or third-party files.
- Do not regenerate shaders, captures, or golden images as a side effect of the audit unless a listed verification command requires a temporary build artifact.
- Do not choose a native RHI strategy or promise a backend feature.
- Do not implement a renderer capability from the inventory.

## Constraints

- Treat a capability as implemented only when the runtime path and its verification exist.
- Report a CPU fallback as a fallback, not as GPU feature completion.
- Separate bgfx public capability from backend-native capability.
- Record current backend, adapter, driver, resolution, configuration, and feature flags for GPU evidence.
- Do not generalize Windows evidence to Linux, macOS, mobile, console, or web.

## Acceptance criteria

- [ ] The output file exists at the exclusive write path.
- [ ] Renderer architecture and ownership are tied to concrete source files and symbols.
- [ ] Existing render-graph, material, resource, frame-pipeline, and GPU-smoke coverage is enumerated.
- [ ] Fallbacks and unsupported paths are visible and are not counted as full implementation.
- [ ] High-risk architecture choices are listed as decision requirements rather than implementation tasks.
- [ ] All executed commands and failures are recorded.
- [ ] No source-controlled file outside the exclusive write scope is changed.

## Verification commands

Run from the repository root:

```powershell
cmake -S . -B build
cmake --build build --config Debug --target kb_renderer_tests
ctest --test-dir build -C Debug -R '^kb_renderer_tests$' --output-on-failure
git diff --check
git status --short
Test-Path -LiteralPath .\docs\roadmap\baseline\renderer.md
Select-String -LiteralPath .\docs\roadmap\baseline\renderer.md -Pattern 'IMPLEMENTED|PARTIAL|MISSING|UNKNOWN'
```

Windowed GPU smoke is evidence only when compatible graphics hardware and an interactive Windows session are available. If run, use the canonical command documented in `tests/README.md` and record all capture metadata. Its absence must be reported, not replaced by an unsupported claim.

## Required evidence

Store the completion record under `Sprint/roadmap/evidence/BASE-RENDER-001/`. Include configured backend information, test output summary, audit report hash, and proof that only the declared write scope changed.

## Failure and escalation

Return the item to `DRAFT` if renderer ownership or backend boundaries cannot be reconciled. Mark it `BLOCKED` when the configured renderer target cannot be built because the required Windows toolchain or bgfx inputs are unavailable. Do not repair renderer failures under this work item.
