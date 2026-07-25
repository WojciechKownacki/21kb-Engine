# 21kb Engine Agentic Execution Policy

## Authority

This file is the canonical human-readable control plane for agent-driven development in this repository. The machine-readable source for sprint and work-item state is [`manifest.json`](manifest.json). Detailed work-item specifications live under [`work-items/`](work-items/).

The canonical sprint task backlog is indexed in [`../README.md`](../README.md) and split into individual files under [`../sprints/`](../sprints/). The archived source inventory is preserved under `Sprint/roadmap/archive/`. Sprint checkboxes express scope and progress, but are not sufficient direct implementation authorization for an agent.

If this file, the manifest, a work-item specification, repository-local `AGENTS.md` instructions, or the current code disagree, work must stop at the smallest safe boundary. The conflict must be reported and resolved in the plan before implementation continues.

## Product contract

21kb Engine is an early-development native C++20 game and editor engine. The current implementation is Windows-first and uses explicit integration layers around bgfx, Flecs, Jolt Physics, Box2D, and miniaudio. The intended desktop direction includes Windows, Linux, and macOS, but a platform is considered supported only after its build, test, packaging, diagnostics, and performance gates are continuously verified.

The program optimizes for:

- correctness and deterministic behavior where a deterministic contract is declared;
- explicit ownership, lifetime, threading, serialization, and failure contracts;
- high CPU and GPU performance supported by measurements;
- native, scalable editor workflows;
- minimal dependency surface and isolated third-party integrations;
- reproducible builds, actionable diagnostics, automated verification, and releasable artifacts;
- maintainability and product quality over feature count.

The following are not automatically committed product scope: consoles, mobile, web, XR, advertising, commerce, cloud streaming, machine-learning training, custom cryptography, custom standard-library replacements, and backend-specific advanced rendering. They remain capability candidates until an architecture decision and a product requirement promote them.

## Outcome-based sprint model

Sprints in this plan are delivery stages, not calendar periods. A sprint closes when its objective and exit evidence are complete. Sprint identifiers are stable and ordered by dependency. Parallel execution is allowed only between work items whose declared read/write scopes and dependencies do not conflict.

A sprint contains bounded work items. A sprint title or sprint-backlog checkbox is never an executable unit of work.

## Agent authorization rule

An agent may implement a work item only when all of the following are true:

1. the work item exists in `Sprint/roadmap/manifest.json`;
2. its manifest status is `READY`;
3. its detailed specification exists and passes the roadmap validator;
4. every `depends_on` work item is `VERIFIED`;
5. no other claimed work item owns an overlapping write scope;
6. the agent has inspected the current code and confirmed that the specification still matches repository reality;
7. required external authority, credentials, SDKs, hardware, product decisions, and licenses are available.

`READY` authorizes only the scope described by that work item. Manifest `write_scope` entries govern source-controlled changes. Declared verification commands may create ignored build, test, capture, and profiling artifacts only in their documented locations; those artifacts must not be committed unless the specification explicitly requires it. `READY` does not authorize adjacent refactors, speculative features, dependency changes, releases, deployments, or edits to other work items.

## Work-item state machine

| State | Meaning | Permitted transition |
|---|---|---|
| `CANDIDATE` | Unreconciled idea from the capability inventory. Not executable. | `DRAFT`, `REJECTED` |
| `DRAFT` | Specification is being prepared or still has unresolved decisions. | `READY`, `REJECTED` |
| `READY` | Validated, dependency-complete, bounded, and authorized for implementation. | `CLAIMED`, `BLOCKED` |
| `CLAIMED` | One agent owns the declared write scope. No implementation has been accepted yet. | `IN_PROGRESS`, `READY`, `BLOCKED` |
| `IN_PROGRESS` | Implementation is underway and evidence is being collected. | `REVIEW`, `BLOCKED` |
| `BLOCKED` | A named dependency, decision, authority, environment, or external state prevents progress. | `READY`, `IN_PROGRESS`, `REJECTED` |
| `REVIEW` | Implementation and author verification are complete; independent review is required. | `IN_PROGRESS`, `VERIFIED` |
| `VERIFIED` | Independent verification passed and all required evidence is recorded. | `DONE`, `IN_PROGRESS` |
| `DONE` | Work is integrated and the sprint-level evidence references it. | No normal transition |
| `REJECTED` | The item will not be implemented under the recorded rationale. | `DRAFT` only through an approved plan change |

State changes must update the manifest in the same change that records the supporting evidence. Checkbox state alone is not authoritative.

## Definition of Ready

A work item may enter `READY` only when it has:

- a globally unique, stable ID and exactly one owning sprint;
- a concrete goal tied to current repository behavior;
- repository context naming the existing modules, symbols, tests, and contracts that matter;
- explicit deliverables and explicit non-goals;
- declared dependencies and blocking decisions;
- expected read scope, write scope, and prohibited changes;
- observable acceptance criteria;
- exact build, test, benchmark, lint, or inspection commands runnable from the repository root;
- numeric performance and memory budgets for hot-path changes, or an explicit statement that no runtime budget is affected;
- platform and build-configuration coverage;
- failure, cancellation, rollback, compatibility, and migration behavior where relevant;
- required evidence artifacts;
- no unresolved architecture decision or product requirement;
- a size that one agent can implement and verify without crossing unrelated subsystem ownership.

If any of these fields cannot be made concrete, the item remains `DRAFT` or becomes a bounded technical spike.

## Definition of Done

A work item may enter `VERIFIED` and then `DONE` only when:

- the requested behavior is implemented without stubs, placeholders, TODOs, dead code, orphaned APIs, or silent fallbacks;
- the smallest relevant build and tests pass, followed by every additional command required by the specification;
- error paths and unsupported paths are tested and produce actionable diagnostics;
- public ownership, lifetime, threading, serialization, and compatibility contracts remain explicit;
- performance-sensitive changes meet their numeric budgets on the declared test hardware and configuration;
- deterministic behavior passes the declared repeatability matrix;
- changed persistent formats include versioning, migration, and backward-compatibility evidence;
- changed dependencies include license, provenance, version, update, and security review;
- generated and packaged artifacts are validated where applicable;
- the implementation diff is reviewed independently for regressions, scope expansion, duplication, and architectural erosion;
- evidence records the commands, exit codes, relevant metrics, artifact paths, platform, configuration, and commit SHA;
- the manifest and sprint evidence are updated without marking unrelated capability candidates complete.

Passing tests is necessary but not sufficient when the acceptance criteria require visual, performance, packaging, migration, or hardware evidence.

## Agent execution protocol

1. Run the roadmap validator.
2. Select one `READY` item whose dependencies are `VERIFIED`.
3. The orchestrator atomically changes the item to `CLAIMED` and records `claimed_by` before the implementation agent edits files.
4. Re-read the work-item specification and the nearest repository instructions.
5. Inspect only the code and contracts needed to validate the stated baseline.
6. If the baseline is stale, stop implementation and ask the orchestrator to return the item to `DRAFT` with evidence.
7. Implement only the declared deliverables and necessary integration changes.
8. Run the specified verification commands and record evidence.
9. Review the diff for unrelated changes and submit the item for `REVIEW`.
10. A separate reviewer or verification pass reproduces the evidence before `VERIFIED`.
11. The orchestrator marks the item `DONE` only when its owning sprint records the accepted result.

The manifest is orchestrator-owned coordination state. An implementation agent must not edit its own status, ownership, dependencies, priority, or scope unless its work item explicitly authorizes roadmap-control changes.

Agents must never resolve ambiguity by silently expanding scope. A technical choice that changes public architecture, dependencies, supported platforms, persistent formats, security posture, or performance targets requires an architecture decision record before implementation.

## Evidence contract

Every completed work item must record:

- work-item ID and commit SHA;
- agent or executor identity;
- operating system, compiler, architecture, build configuration, and relevant hardware;
- exact commands and exit codes;
- test counts and failures;
- benchmark inputs, repetitions, baseline, result, variance, and threshold;
- created artifacts and their repository-relative paths;
- known limitations and residual risks;
- reviewer identity and verification outcome.

Evidence belongs under `Sprint/roadmap/evidence/<work-item-id>/`. Large generated binaries, captures, and logs must follow repository artifact policy rather than being committed without review.

## Architecture and product decision policy

An architecture decision record is mandatory before:

- adding or replacing a third-party dependency;
- creating an engine-wide container, allocator, smart pointer, string, task runtime, serialization framework, or scripting backend;
- changing the renderer abstraction or introducing native-backend escape paths;
- changing ABI, asset, scene, save, network, plugin, or scripting compatibility;
- promising cross-platform determinism;
- enabling untrusted code, content, network input, plugins, commerce, telemetry, or cryptography;
- committing a new supported platform or hardware tier.

Every decision must document context, constraints, considered alternatives, measured evidence where applicable, consequences, rollback strategy, and affected work items.

## Program quality gates

| Gate | Required evidence |
|---|---|
| Correctness | Unit, integration, regression, negative-path, and smoke coverage appropriate to the change |
| Build quality | Supported configurations build reproducibly with project warning policy and no new diagnostics |
| Performance | Numeric CPU, GPU, memory, I/O, load-time, and package-size budgets on declared hardware |
| Determinism | Declared boundary, compiler/platform matrix, stable seeds and ordering, repeatability result |
| Data compatibility | Version identifiers, migration path, corrupt/unsupported input behavior, round-trip evidence |
| Concurrency | Ownership model, synchronization strategy, cancellation, shutdown, race/deadlock validation |
| Diagnostics | Logs, counters, traces, debug views, failure messages, and actionable unsupported-path reporting |
| Security | Threat model, input validation, dependency review, secret handling, privilege boundary, fuzzing where applicable |
| Tooling | Editor/CLI workflow, undo/redo or transaction behavior, recovery, automation, source-control safety |
| Release | Cooked and packaged artifact, startup smoke, required assets, licenses, symbols, crash reporting |

Each work item selects the applicable gates. Declaring a gate not applicable requires a reason in its specification.

## Delivery stage registry

| Stage | Outcome | Depends on | Status |
|---|---|---|---|
| `S00` | Verified repository baseline, reconciled capability ownership, agent governance, and first executable vertical-slice plan | None | `ACTIVE` |
| `S01` | Reproducible toolchain, CI, tests, diagnostics, crash handling, profiling, and dependency governance | `S00` | `PLANNED` |
| `S02` | Stable core runtime: HAL, memory, jobs, timing, configuration, reflection, serialization, and module contracts | `S01` | `PLANNED` |
| `S03` | Versioned content foundation: VFS, asset registry, import, cooking, streaming handles, scenes, saves, and prefabs | `S02` | `PLANNED` |
| `S04` | Validated renderer foundation and a deterministic rendered-scene runtime slice | `S02`, `S03` | `PLANNED` |
| `S05` | Native authoring slice: editor shell, content browser, inspector, scene editing, undo/redo, play mode, and recovery | `S03`, `S04` | `PLANNED` |
| `S06` | Packaged playable vertical slice with input, camera, physics, audio, UI, save/load, diagnostics, and release smoke | `S04`, `S05` | `PLANNED` |
| `S07` | Measured scale foundation: culling, LOD, streaming, world partitioning, residency, and performance budgets | `S06` | `PLANNED` |
| `S08` | Production gameplay stack: animation, scripting, navigation, AI, VFX, media, localization, and accessibility | `S06`, `S07` | `PLANNED` |
| `S09` | Large-world authoring: terrain, foliage, atmosphere, weather, water, PCG, and ecosystem integration | `S07`, `S08` | `PLANNED` |
| `S10` | Versioned multiplayer, replay, dedicated-server, interest-management, security, and network simulation | `S06`, `S08` | `PLANNED` |
| `S11` | Capability-gated advanced rendering, XR, 2D, compute, capture, and optional platform features | `S07`, `S08` | `PLANNED` |
| `S12` | Approved platform expansion, online services, LiveOps, distribution, certification, and isolated commercial modules | `S10`, `S11` | `PLANNED` |

Later sprints do not authorize all similarly named capability candidates. Their exact scope is selected only through promoted work items and architecture decisions.

## Active ready queue

| Work item | Purpose | Write scope | Status |
|---|---|---|---|
| [`BASE-CORE-001`](work-items/BASE-CORE-001.md) | Reconcile core runtime, ECS, scene, asset, save, scripting, physics, and audio foundations with repository reality | `Sprint/roadmap/baseline/core-runtime.md` | `READY` |
| [`BASE-RENDER-001`](work-items/BASE-RENDER-001.md) | Reconcile renderer architecture, resource, render-graph, shader, material, and GPU-test foundations | `Sprint/roadmap/baseline/renderer.md` | `READY` |
| [`BASE-TOOLS-001`](work-items/BASE-TOOLS-001.md) | Reconcile editor, hub, CLI, content workflow, automation, and recovery foundations | `Sprint/roadmap/baseline/tools.md` | `READY` |
| [`BASE-BUILD-001`](work-items/BASE-BUILD-001.md) | Reconcile build, platform, test, packaging, dependency, and release infrastructure | `Sprint/roadmap/baseline/build-release.md` | `READY` |
| [`BASE-INTEGRATE-001`](work-items/BASE-INTEGRATE-001.md) | Produce the canonical baseline and promote the first production implementation work items | `Sprint/roadmap/Baseline.md`, roadmap control files | `BLOCKED` |

The four ready baseline audits have disjoint write scopes and may run in parallel. `BASE-INTEGRATE-001` remains blocked until all four audits are independently verified.

## Plan change control

Any change to sprint objectives, dependencies, supported platforms, product scope, work-item status, public architecture, or quality gates must:

1. explain why the current plan is insufficient;
2. identify affected work items and evidence;
3. preserve stable IDs;
4. update the manifest and human-readable plan together;
5. pass the roadmap validator;
6. avoid silently converting capability candidates into authorized work.

Removing completed evidence, weakening acceptance criteria after implementation, or marking work complete solely to unblock dependent items is prohibited.

## Current authorization

Only the four `READY` baseline audit items listed above are authorized. No engine feature from the capability inventory is currently authorized for implementation.
