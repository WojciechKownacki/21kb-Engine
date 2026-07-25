# BASE-CORE-001 — Reconcile Core Runtime Foundations

| Field | Value |
|---|---|
| Status | `READY` |
| Sprint | `S00` |
| Priority | `P0` |
| Depends on | None |
| Blocks | `BASE-INTEGRATE-001` |
| Change type | Read-only architecture audit with one documentation artifact |
| Runtime budget impact | None; this item must not change runtime code |
| Platform coverage | Current Windows build evidence; repository evidence for intended Linux and macOS support |
| Required gates | Correctness, build quality, determinism, data compatibility, concurrency, diagnostics |

## Goal

Produce an evidence-backed baseline of the existing core runtime so later agents extend canonical systems instead of creating parallel implementations. The baseline must cover core utilities, ECS, scene runtime, assets, saves, prefabs, scripting, physics, audio, serialization, reflection, jobs, and their existing automated tests.

## Repository context

The repository already contains a C++20 engine library, Flecs integration, scene and prefab code, asset runtime code, save tests, Jolt and Box2D integrations, miniaudio integration, scripting code, and extensive engine tests. The capability inventory still contains broad unchecked requests for several of these systems. This item determines what is implemented, partial, absent, duplicated, or architecturally unresolved.

Expected read scope:

- `README.md`
- `CMakeLists.txt`
- `sources/engine/`
- `sources/plugins/physics_jolt/`
- `sources/plugins/physics_box2d/`
- `sources/plugins/audio_miniaudio/`
- `sources/engine/tests/`
- `tests/run-engine-tests.ps1`
- relevant entries in `Sprint/roadmap/CapabilityInventory.md`

Exclusive source-controlled write scope:

- `Sprint/roadmap/baseline/core-runtime.md`
- `Sprint/roadmap/evidence/BASE-CORE-001/`

## Deliverables

Create `Sprint/roadmap/baseline/core-runtime.md` containing:

1. the exact build and test commands executed, their exit codes, configuration, compiler, and host;
2. a subsystem table with `IMPLEMENTED`, `PARTIAL`, `MISSING`, or `UNKNOWN` status;
3. canonical public headers, implementation owners, test targets, and plugin boundaries for every audited subsystem;
4. ownership, lifetime, threading, serialization, compatibility, and determinism contracts found in code;
5. capability-inventory groups and representative entries that are already implemented, partial, duplicates, obsolete, or unsafe to execute directly;
6. concrete gaps expressed as proposed work-item titles, without promoting them to `READY`;
7. unresolved architecture decisions and evidence explaining why each decision is needed;
8. current test failures, unsupported paths, and residual risks without attempting to repair them.

## Non-goals

- Do not modify production code, tests, CMake, third-party code, or the capability inventory.
- Do not implement missing features or repair failures discovered during the audit.
- Do not redesign ECS, scene, physics, audio, assets, saves, scripting, reflection, serialization, memory, or job systems.
- Do not mark any capability candidate complete or promote a new work item.

## Constraints

- Treat source code and passing tests as stronger evidence than names, comments, or roadmap text.
- Distinguish an engine-owned contract from a third-party capability.
- Record conflicting or duplicate ownership explicitly.
- Do not claim cross-platform or cross-machine determinism without executed evidence on the stated matrix.
- Do not infer production readiness from the existence of a type, test fixture, or plugin.

## Acceptance criteria

- [ ] The output file exists at the exclusive write path.
- [ ] Every subsystem named in the goal has a status, canonical owner, evidence path, and test evidence.
- [ ] The report cites concrete repository-relative files and symbols rather than only feature names.
- [ ] Existing Jolt, Box2D, Flecs, asset, save, prefab, scene, audio, and scripting foundations are reconciled against the inventory.
- [ ] All executed commands and failures are recorded without hiding unavailable or unsupported paths.
- [ ] No source-controlled file outside the exclusive write scope is changed.
- [ ] Proposed gaps are bounded and remain unpromoted.

## Verification commands

Run from the repository root:

```powershell
.\tests\run-engine-tests.ps1
git diff --check
git status --short
Test-Path -LiteralPath .\docs\roadmap\baseline\core-runtime.md
Select-String -LiteralPath .\docs\roadmap\baseline\core-runtime.md -Pattern 'IMPLEMENTED|PARTIAL|MISSING|UNKNOWN'
```

The engine test command may expose an existing failure. The audit is verifiable when the exact failure and exit code are recorded; the failure must not be relabeled as a pass.

## Required evidence

Store the completion record under `Sprint/roadmap/evidence/BASE-CORE-001/` according to the evidence contract in `Sprint/roadmap/AgentExecutionPolicy.md`. The evidence must include the audit report hash and confirmation that only the declared write scope changed.

## Failure and escalation

Return the item to `DRAFT` if canonical ownership cannot be determined from the repository. Mark it `BLOCKED` if the engine tests cannot be configured or launched because a required toolchain or dependency is unavailable. Do not expand the audit into implementation.
