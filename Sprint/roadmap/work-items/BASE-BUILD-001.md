# BASE-BUILD-001 — Reconcile Build and Release Foundations

| Field | Value |
|---|---|
| Status | `READY` |
| Sprint | `S00` |
| Priority | `P0` |
| Depends on | None |
| Blocks | `BASE-INTEGRATE-001` |
| Change type | Read-only build and release audit with one documentation artifact |
| Runtime budget impact | None; this item must not change build or runtime behavior |
| Platform coverage | Current host evidence plus declared Windows, Linux, and macOS intentions |
| Required gates | Build quality, performance infrastructure, security, diagnostics, release |

## Goal

Produce an evidence-backed baseline of configuration, compilation, tests, quality gates, dependencies, licenses, platform abstraction, shader and asset build steps, packaging, regression scripts, crash diagnostics, and releasable artifacts.

## Repository context

The project uses CMake and C++20, has multiple test scripts and CTest targets, vendors major third-party libraries, and currently restricts the renderer surface integration to Win32. The README describes Windows, Linux, and macOS as intended targets while also declaring early development. This audit must distinguish implemented, continuously verified, manually buildable, intended, and unsupported states.

Expected read scope:

- `README.md`
- `CMakeLists.txt`
- `CMake/`
- `.github/`
- `tests/`
- `tools/`
- `third_party/THIRD_PARTY_LICENSES.md`
- first-party `CMakeLists.txt` files under `sources/`
- platform, build, packaging, deployment, and dependency entries in `Sprint/roadmap/CapabilityInventory.md`

Exclusive source-controlled write scope:

- `Sprint/roadmap/baseline/build-release.md`
- `Sprint/roadmap/evidence/BASE-BUILD-001/`

## Deliverables

Create `Sprint/roadmap/baseline/build-release.md` containing:

1. supported and intended host/target matrix with evidence level for each cell;
2. compiler, language-standard, architecture, configuration, renderer, test, editor, CLI, plugin, and packaging targets;
3. canonical clean configure, incremental build, test, regression, and smoke commands;
4. CI workflows, quality gates, warning policy, sanitizers, static analysis, coverage, fuzzing, reproducibility, caching, and artifact retention status;
5. third-party dependency inventory with source, pinned version evidence, license location, update mechanism, and security-review gaps;
6. shader, asset, cook, package, symbols, crash-reporting, installer, and release-manifest status;
7. numeric-budget infrastructure currently available and the missing hardware baseline needed before performance work is promoted;
8. proposed bounded work-item titles for genuine infrastructure gaps without promotion.

## Non-goals

- Do not edit build scripts, CI, dependencies, tests, licenses, source files, or manually alter generated build trees. The declared regression command may recreate its documented ignored build directory.
- Do not download SDKs, update dependencies, alter compiler flags, or enable new platforms.
- Do not repair build or test failures under this work item.
- Do not claim platform support from a configuration option or vendored backend alone.

## Constraints

- Use `SUPPORTED` only for a target with repeatable build, test, packaging, and diagnostic evidence.
- Use `INTENDED` for documented direction without continuous verification.
- Record generated and ignored build artifacts separately from source-controlled release inputs.
- Treat missing license, provenance, version, or update information as a release blocker.
- Record the exact host and toolchain used for every executed command.

## Acceptance criteria

- [ ] The output file exists at the exclusive write path.
- [ ] The platform matrix distinguishes `SUPPORTED`, `PARTIAL`, `INTENDED`, and `UNSUPPORTED`.
- [ ] Canonical build and verification commands are tied to existing scripts or CMake targets.
- [ ] CI, quality, dependency, license, security, packaging, and artifact gaps are explicit.
- [ ] The report does not present the Win32 renderer path as cross-platform completion.
- [ ] Every executed command and failure is recorded.
- [ ] No source-controlled file outside the exclusive write scope is changed.

## Verification commands

Run from the repository root:

```powershell
.\tests\run-regression.ps1
git diff --check
git status --short
Test-Path -LiteralPath .\docs\roadmap\baseline\build-release.md
Select-String -LiteralPath .\docs\roadmap\baseline\build-release.md -Pattern 'SUPPORTED|PARTIAL|INTENDED|UNSUPPORTED'
```

`run-regression.ps1` is expected to perform its documented clean regression flow. Record its full exit status and artifact location. A failure is baseline evidence and must not be hidden or repaired under this item.

## Required evidence

Store the completion record under `Sprint/roadmap/evidence/BASE-BUILD-001/`. Include host and toolchain identity, regression summary, dependency and license references, the audit report hash, and proof that only the declared write scope changed.

## Failure and escalation

Return the item to `DRAFT` if the canonical build topology cannot be established. Mark it `BLOCKED` when the required compiler, SDK, PowerShell environment, or vendored inputs are unavailable. Do not change the environment or dependency set without separate authorization.
