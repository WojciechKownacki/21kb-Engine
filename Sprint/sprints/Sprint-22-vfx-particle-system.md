# Sprint 22 · VFX / Particle System

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver an authorable CPU- and GPU-driven visual-effects system with deterministic spawning where required, scalable simulation and rendering, world integration, LOD, budgets, debugging, and reusable effect assets.

## Effect Graph & Emitters

- [ ] Add a node-based effect graph
- [ ] Add emitters with configurable spawn rate and bursts
- [ ] Add emitter shapes (point, sphere, box, cone, mesh, spline)
- [ ] Add multiple emitters per effect
- [ ] Add sub-emitters spawned from particle events
- [ ] Add exposed parameters driven by gameplay
- [ ] Add reusable effect templates and presets
- [ ] Add effect lifetime, looping, and one-shot modes
- [ ] Add effect graph versioning and migration
- [ ] Add effect-graph debugging

## Particle Modules

- [ ] Add initial and over-life position, velocity, and acceleration modules
- [ ] Add color, opacity, and size-over-life modules
- [ ] Add rotation and angular-velocity modules
- [ ] Add force, drag, gravity, and turbulence modules
- [ ] Add curl-noise and vector-field modules
- [ ] Add collision modules against depth and the world
- [ ] Add attractor and orbit modules
- [ ] Add scale-by-speed and stretch modules
- [ ] Add custom-expression modules
- [ ] Add module ordering and stacking

## GPU Particle Simulation

- [ ] Add GPU-simulated particles with large counts
- [ ] Add a CPU-simulated path for small or gameplay-critical effects
- [ ] Add persistent particle buffers and pooling
- [ ] Add GPU spawn and death lists
- [ ] Add indirect draw from simulated particles
- [ ] Add depth-buffer and distance-field collision on GPU
- [ ] Add sorting for transparent particles
- [ ] Add particle simulation budgets

## Particle Renderers

- [ ] Add sprite and billboard renderers
- [ ] Add mesh-particle renderers
- [ ] Add ribbon and trail renderers
- [ ] Add beam renderers
- [ ] Add light-emitting particles
- [ ] Add decal-spawning particles
- [ ] Add lit, unlit, and volumetric particle materials
- [ ] Add motion blur and soft-particle depth fade
- [ ] Add sprite-sheet and flipbook animation

## VFX Runtime, LOD & Integration

- [ ] Add effect spawning and pooling at runtime
- [ ] Add effect LOD by distance and screen coverage
- [ ] Add effect culling and budgets
- [ ] Add fixed-cost and scalable-quality effects
- [ ] Add attachment of effects to entities and sockets
- [ ] Add effect events driving gameplay and audio
- [ ] Add integration with physics, wind, and weather
- [ ] Add a scripting API for spawning and controlling effects
- [ ] Add an effect-authoring editor with live preview
- [ ] Add effect performance profiling
- [ ] Add effect debugging and visualization
- [ ] Add effect tests
