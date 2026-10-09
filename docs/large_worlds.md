# Large world coordinates

A 21kb world can span thousands of kilometres while objects near the camera keep
sub-millimetre precision. Translations are kept in double precision where it matters (scene
transforms, physics, scene files) and every float system (GPU, audio mixer, navigation,
particles) works relative to an origin near the place it is looking at.

This page describes how it works and how to use it from engine and game code.

## Precision at a glance

A float has a 24-bit mantissa: 10 000 km (1e7 m) from the origin neighbouring floats are 1 m
apart, 16 km out they are 2 mm apart. The engine therefore never relies on a single float for
an absolute world position far from the origin:

| Data | Representation | Precision at 1e7 m |
|---|---|---|
| Transform translations (local and world) | float view plus a float residual | better than 1e-7 m |
| Physics bodies, queries, contacts | Jolt built with `JPH_DOUBLE_PRECISION` | double |
| GPU positions, view matrix, lights | float, relative to the render origin | float precision within the 1 km around the camera |
| Audio listener, sources, voices | float, relative to the audio origin | idem, around the listener |
| Navigation graph and agents | float, relative to `NavMesh::origin` | idem, around the graph origin |
| Particles | float, relative to the particle simulation origin | idem, around the camera |
| Scene files (version 42 and newer) | float64 local translations | double |

Scenes that stay within a kilometre or two of the world origin behave and render exactly as
before: none of the mechanisms below changes a value there.

## Transforms

`kb::scene::TransformComponent` is unchanged: `localPosition` and `worldPosition` stay floats
and every existing reader keeps working. The part of a translation below float precision is
kept beside it, in a per-scene residual table (`SceneTransformResiduals`, indexed like the
entities), so the precise translation is `double(position) + double(residual)`. A scene in
which no entity carries a residual has an empty table and pays nothing for it.

Use the double-precision accessors wherever positions can be far from the origin:

```cpp
#include "engine/math/DVec3.hpp"

scene.Transforms().SetLocalTranslation(entity, kb::math::DVec3{ 1.0e7, 0.0, 1.0e7 + 0.25 });
const kb::math::DVec3 local = scene.Transforms().LocalTranslation(entity);
const kb::math::DVec3 world = scene.Transforms().WorldTranslation(entity);   // fresh after the transform sync
const bool any = scene.Transforms().HasDoublePrecisionTranslations();
```

The same accessors exist on `SceneTransformQueries` and on `SceneSystemTransformAccess`
(systems), `TransformRowRange::SetLocal` has a `DVec3` overload for parallel transform
passes, and `SceneRuntime::InterpolatedWorldTranslation` interpolates fixed-step poses in
double precision. `kb::math::DVec3` (engine/math/DVec3.hpp) carries the arithmetic,
`RelativeTo(position, origin)` (a float offset computed in double precision) and
`RotateDouble`.

### Writing the float view

Code that writes `localPosition` directly still works:

- A residual is used only while it fits its float view: per axis, while
  `|residual| <= |view| * 2^-24` (`kb::math::FittingResidual`). A float write that moves the
  translation away (say from 1e7 to 5) drops it; a write that keeps its magnitude, such as
  changing only the rotation of a copy obtained with `Get`, keeps it. Either way the result
  is within float precision of what the code wrote.
- `SetLocalTranslation` writes the float view through the normal `Set` path (change
  notifications see a normal write) and then stores the residual.

### Composition

The transform sync composes children in float as it always did. A child is then recomposed
in double precision (`ComposeChildTranslation`) when its local translation or its parent's
world translation carries a residual, or when its float world translation is 2048 m or more
from the origin: its world translation is the parent's precise world translation plus the
precise local translation scaled and rotated into the parent's frame. Everything nearer the
origin keeps the float result bit for bit.

## Rendering: camera-relative

Each `RenderScene` has a render origin (`RenderScene::RenderOrigin`). Every position the GPU
sees (model matrices, instance data, decals, lights, the view matrix and everything derived
from it, `u_cameraPosition`) is the world position minus the render origin, computed in
double precision and then rounded to float.

- The origin starts at the world origin. When the viewer is further than
  `RenderOriginPolicy::rebaseDistance` (1024 m) from it along an axis, it moves to the
  viewer's position rounded to `RenderOriginPolicy::gridStep` (1024 m).
  `Renderer::SetRenderOriginPolicy` changes both.
- A move marks every proxy for a transform re-pull in the same frame, re-expresses the
  previous frame's view-projection (motion vectors and temporal accumulation stay
  continuous) and calls the listeners added with `RenderScene::AddRenderOriginListener`.
- Hosts describe cameras and editor overlays in world space as before; the renderer
  converts them. The eye of a world-space view matrix is only as precise as the matrix's
  float translation, so a host whose camera is far from the origin passes its precise eye in
  `RenderSceneSubmitDesc::cameraOverrideEye`.
- `SceneRenderFeedback` stays in world space: `WorldToScreen(DVec3)` and the published
  visibility bounds take absolute positions, `SceneRenderFeedback::RenderOrigin` reports the
  origin of the published frame. World-space UI canvases are placed through it.
- Material graphs read world positions (`worldPos`, object position and bounds, camera
  position) as render-space positions plus the render origin wrapped to 8192 m
  (`u_renderOriginOffset`). Within 8 km of the world origin these are the exact world
  positions; further out, world-aligned patterns stay seamless when their period divides
  8192 m.

## Physics

The vendored Jolt is configured with `DOUBLE_PRECISION=ON`, which makes
`JPH_DOUBLE_PRECISION` a public compile definition of the `Jolt` target, so every target
that includes Jolt headers (only the Jolt physics plugin) agrees on it. Bodies and characters
are created and moved from the precise world translations, simulated poses are written back
through `SetLocalTranslation`, and contact points are reported in double precision
(`PendingCollisionEvent::worldPoint`). Queries take double-precision origins:

```cpp
kb::scene::RaycastAllNonAllocPrecise(scene, kb::math::DVec3{ 1.0e7, 2.0, 1.0e7 }, direction, 10.0F, mask, hits);
// hits[i].worldPoint is the double-precision hit point; hits[i].point is its float view.
```

`PhysicsBackend` has `...Precise` variants of the casts, overlaps and closest-point
queries; the float variants are unchanged.

## Scene files and prefabs

- From scene file version 42 each node's local translation is stored as three float64
  values. Older files store float32 values and load unchanged; they are written as version
  42 the next time they are saved.
- Prefab assets keep the residual of a node's translation in `localPositionResidual`
  (written only when it is not zero), and prefab override values of a translation are
  written with enough digits to restore the double exactly. Instantiation, duplication,
  undo and prefab override comparison work on the precise translation.

## Audio

The mixer spatialises in float, so the miniaudio playback backend hands it positions
relative to an audio origin near the listener (`MiniaudioAudioSpace`). The origin follows
the listener with the same 1024 m policy as the render origin; when it moves, kept positions
(previous listener and source positions, free-standing voices) move with it, so velocities
and doppler stay continuous. Occlusion rays are cast with the double-precision physics
queries.

## Navigation

`NavMesh::origin` is the world position a navigation graph is centred on: node positions,
agent destinations, `SceneNavigation::NearestNode` and `SceneNavigation::AgentPath` are in
the graph's space (world position minus the origin). Agents read and write their
transforms in double precision, so a graph built around a far region moves its agents with
float precision there. The default origin (zero) keeps graphs in world space.

## Particles

Particles simulate in float relative to the scene's particle simulation origin
(`ParticlePlayback::SimulationOrigin`). The renderer sets it to its render origin every
frame; the CPU backend moves live particles, owner transforms and queued events with it at
its next fixed step, and collision planes (authored in world space) are re-expressed for it.
Render snapshots carry the origin they were simulated around
(`ParticleRenderSnapshot::Origin`) and GPU emitter commands carry theirs
(`ParticleGpuEmitterCommand::simulationOrigin`); the renderer adds the difference to its
render origin while the two disagree (`ParticleRenderOffset`). Positions returned by
`ParticlePlayback::LiveParticleStates` and the owner transforms given to
`ParticlePlayback::ConfigureComponent` are in the simulation space as well.

GPU-simulated emitters keep their birth records on the GPU, relative to the render origin.
When the render origin moves, the live particles of world-space GPU emitters are cleared
(their emitters keep emitting); local-space GPU emitters follow their owner and keep theirs.

## Limits

- One render origin per scene: viewports looking at places kilometres apart share it, and
  the ones far from it render with reduced precision.
- Positions exposed as floats keep float precision: the float `Vec3` accessors
  (`worldPosition`), the script API, the editor's fly camera and gizmos, and the systems
  that still work on float world positions (animation constraints and IK, region shape and
  guide curve queries, physics debug drawing). Use the `DVec3` accessors and the
  `...Precise` queries for positions far from the origin.
- Rotations, scales and local offsets between parent and child are floats; a child placed
  thousands of kilometres from its parent has float precision relative to the parent.
