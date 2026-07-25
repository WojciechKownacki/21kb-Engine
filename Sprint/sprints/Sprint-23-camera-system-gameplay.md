# Sprint 23 · Camera System (Gameplay)

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver a gameplay camera framework with composable modes, rigs, blending, collision, effects, deterministic control, debugging, and clean integration with rendering, input, animation, physics, UI, and replay.

## Camera Manager & Blending

- [ ] Add a gameplay camera manager
- [ ] Add a camera stack with priorities
- [ ] Add smooth blends between active cameras
- [ ] Add blend curves and durations
- [ ] Add per-camera settings (field of view, offset, lag)
- [ ] Add camera activation and deactivation events
- [ ] Add handoff to and from cinematic cameras
- [ ] Add multiple cameras for split-screen
- [ ] Add camera debug visualization

## Camera Modes

- [ ] Add a first-person camera mode
- [ ] Add a third-person follow camera
- [ ] Add an orbit and free-look camera
- [ ] Add a top-down and isometric camera
- [ ] Add a side-scroll camera
- [ ] Add a fixed and rail camera
- [ ] Add a target-lock and look-at camera
- [ ] Add smooth transitions between modes
- [ ] Add per-mode tuning presets

## Camera Rigs, Collision & Effects

- [ ] Add a camera boom/arm with configurable length
- [ ] Add camera collision that pulls in on obstacles
- [ ] Add spring and lag smoothing
- [ ] Add camera shake sources and profiles
- [ ] Add recoil, impulse, and hit-reaction shake
- [ ] Add auto-framing and composition rules
- [ ] Add dynamic field-of-view by speed and state
- [ ] Add camera-space screen effects (vignette on hit, speed lines)
- [ ] Add zoom and aim-down-sights transitions
- [ ] Add camera-rig authoring and presets
- [ ] Add a scripting API for cameras
- [ ] Add camera tests
