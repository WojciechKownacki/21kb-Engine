# BASE-INTEGRATE-001 — Integrate the Canonical Baseline and Promote Implementation Work

| Field | Value |
|---|---|
| Status | `BLOCKED` |
| Sprint | `S00` |
| Priority | `P0` |
| Depends on | `BASE-CORE-001`, `BASE-RENDER-001`, `BASE-TOOLS-001`, `BASE-BUILD-001` |
| Blocks | Sprint `S01` promotion |
| Change type | Roadmap integration and controlled work-item promotion |
| Runtime budget impact | None; this item must not change product code |
| Platform coverage | The verified matrix reported by the four baseline audits |
| Required gates | All program gates at planning-evidence level |

## Goal

Integrate the four independently verified baseline audits into one canonical repository baseline, resolve contradictory ownership and status claims, classify high-value capability candidates, and promote only the first bounded production implementation work items needed for Sprint `S01`.

## Repository context

This item is intentionally blocked. It must not start until all dependencies are `VERIFIED`, their evidence exists, and their write scopes are clean. The integration must use repository evidence rather than assuming the capability inventory or README is current.

Expected read scope:

- `Sprint/README.md`
- `Sprint/roadmap/manifest.json`
- `Sprint/roadmap/CapabilityInventory.md`
- `Sprint/roadmap/baseline/core-runtime.md`
- `Sprint/roadmap/baseline/renderer.md`
- `Sprint/roadmap/baseline/tools.md`
- `Sprint/roadmap/baseline/build-release.md`
- evidence directories for all dependency work items
- repository files cited by the four baseline audits

Exclusive source-controlled write scope:

- `Sprint/roadmap/Baseline.md`
- `Sprint/roadmap/manifest.json`
- `Sprint/README.md`
- new Sprint `S01` work-item specifications under `Sprint/roadmap/work-items/`
- `Sprint/roadmap/evidence/BASE-INTEGRATE-001/`

## Deliverables

1. Create `Sprint/roadmap/Baseline.md` as the canonical implemented/partial/missing system map.
2. Resolve contradictory ownership, platform, test, and capability-status claims with cited evidence.
3. Define the supported platform and toolchain matrix for Sprint `S01`.
4. Define initial numeric quality budgets or create bounded measurement work items when no trustworthy baseline exists.
5. Classify the capability candidates needed by the first production vertical slice.
6. Create bounded Sprint `S01` work-item specifications that satisfy Definition of Ready.
7. Update the manifest so only dependency-complete, validator-clean items become `READY`.
8. Record rejected and deferred candidates with rationale; do not delete them from the inventory.

## Non-goals

- Do not modify product code, tests, build logic, dependencies, or generated artifacts.
- Do not promote later feature work merely because it is desirable.
- Do not weaken gates to make a candidate ready.
- Do not resolve architecture choices without an architecture decision record.

## Constraints

- Preserve stable IDs and evidence history.
- A candidate may map to multiple work items, but each promoted work item must have one owner and bounded write scope.
- Duplicate candidates must converge on one canonical implementation owner.
- Work-item dependencies must form an acyclic graph.
- Parallel-ready work items must have non-overlapping write scopes or an explicit coordination owner.

## Acceptance criteria

- [ ] Every dependency is `VERIFIED` and has complete evidence.
- [ ] `Sprint/roadmap/Baseline.md` reconciles all four audit domains.
- [ ] Sprint `S01` has an explicit outcome, dependency-complete work items, and exact verification commands.
- [ ] Every new `READY` item passes the roadmap validator.
- [ ] The manifest dependency graph is acyclic and all referenced specs exist.
- [ ] Capability candidates are promoted, deferred, or rejected without being silently discarded.
- [ ] No product-code file is changed.

## Verification commands

Run from the repository root:

```powershell
.\Sprint\tools\validate-sprint-plan.ps1
git diff --check
git status --short
Test-Path -LiteralPath .\docs\roadmap\Baseline.md
```

All commands must pass before this item enters `REVIEW`.

## Required evidence

Store the completion record under `Sprint/roadmap/evidence/BASE-INTEGRATE-001/`. Include hashes of the four baseline inputs, the integrated baseline, the validated manifest, the promoted specification set, and the reviewer decision.

## Failure and escalation

Keep the item `BLOCKED` while any dependency is not `VERIFIED`. Return it to `DRAFT` if baseline reports conflict materially or if product decisions are required to choose Sprint `S01` scope. Do not manufacture consensus from incomplete evidence.
