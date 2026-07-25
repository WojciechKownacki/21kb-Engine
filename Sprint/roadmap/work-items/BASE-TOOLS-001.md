# BASE-TOOLS-001 — Reconcile Editor and Tool Foundations

| Field | Value |
|---|---|
| Status | `READY` |
| Sprint | `S00` |
| Priority | `P0` |
| Depends on | None |
| Blocks | `BASE-INTEGRATE-001` |
| Change type | Read-only editor and tools audit with one documentation artifact |
| Runtime budget impact | None; this item must not change executable code |
| Platform coverage | Current native Windows editor and repository evidence for portable tooling |
| Required gates | Correctness, build quality, data compatibility, diagnostics, tooling, release |

## Goal

Produce an evidence-backed baseline of the native editor, hub, CLI, content browser, hierarchy, inspector, docking, viewport, material authoring, scene and prefab workflows, command history, play mode, automation, recovery, and source-control safety.

## Repository context

The repository already contains native editor code, editor tests, a hub, CLI commands and tests, asset-browser and prefab workflows, material graph tooling, test scripts, and a Windows editor executable. The inventory still requests many of these capabilities at multiple levels of detail. The audit must determine canonical ownership, actual workflow completeness, and gaps that can be promoted safely.

Expected read scope:

- `README.md`
- `sources/editor/`
- `sources/editor/tests/`
- `sources/hub/`
- `sources/tools/`
- `samples/`
- `tests/run-editor-tests.ps1`
- `tests/run-material-graph-release-gate.ps1`
- editor and tooling entries in `Sprint/roadmap/CapabilityInventory.md`

Exclusive source-controlled write scope:

- `Sprint/roadmap/baseline/tools.md`
- `Sprint/roadmap/evidence/BASE-TOOLS-001/`

## Deliverables

Create `Sprint/roadmap/baseline/tools.md` containing:

1. editor, hub, CLI, and supporting tool targets with entry points and owners;
2. the current document, command, transaction, undo/redo, selection, property, asset, scene, prefab, play-mode, and recovery models;
3. user workflows that have end-to-end automated coverage and workflows verified only at unit level;
4. current native-platform assumptions and portable boundaries;
5. automation scripts, test targets, smoke paths, diagnostic surfaces, and failure recovery;
6. inventory entries classified as implemented, partial, duplicate, missing, or dependent on a product or architecture decision;
7. workflow gaps expressed as proposed bounded work-item titles without promotion;
8. merge-conflict, source-control, autosave, crash-recovery, and multi-user risks found in the current design.

## Non-goals

- Do not edit editor, hub, CLI, test, sample, build, or third-party files.
- Do not redesign the editor shell or introduce a UI framework.
- Do not repair workflows or add missing commands discovered during the audit.
- Do not create user-facing placeholder panels or mock behavior.

## Constraints

- A visible panel is not a complete workflow without persistence, transactions, error handling, and verification.
- Distinguish editor-only state from project and runtime state.
- Record whether undo/redo, autosave, reload, and play-mode transitions preserve ownership and data.
- Treat screenshots and manual observations as supplementary evidence, not substitutes for repeatable tests.
- Do not claim cross-platform editor support from platform-neutral headers alone.

## Acceptance criteria

- [ ] The output file exists at the exclusive write path.
- [ ] Every workflow named in the goal has status, owner, evidence path, and verification coverage.
- [ ] The report identifies canonical editor, hub, and CLI boundaries.
- [ ] Existing tests and release-gate scripts are mapped to the workflows they protect.
- [ ] Duplicate and already implemented inventory entries are identified.
- [ ] Risks around transactions, persistence, recovery, and source control are explicit.
- [ ] No source-controlled file outside the exclusive write scope is changed.

## Verification commands

Run from the repository root:

```powershell
.\tests\run-editor-tests.ps1
cmake --build build --config Debug --target kb_cli_tests
ctest --test-dir build -C Debug -R '^kb_cli_tests$' --output-on-failure
git diff --check
git status --short
Test-Path -LiteralPath .\docs\roadmap\baseline\tools.md
Select-String -LiteralPath .\docs\roadmap\baseline\tools.md -Pattern 'IMPLEMENTED|PARTIAL|MISSING|UNKNOWN'
```

Record any pre-existing failure exactly. Do not change production or test code to make the audit pass.

## Required evidence

Store the completion record under `Sprint/roadmap/evidence/BASE-TOOLS-001/`. Include test summaries, workflow evidence references, the audit report hash, and proof that only the declared write scope changed.

## Failure and escalation

Return the item to `DRAFT` if editor document ownership or transaction boundaries cannot be determined. Mark it `BLOCKED` when required editor or CLI targets cannot be configured or launched. Do not expand the scope into fixes.
