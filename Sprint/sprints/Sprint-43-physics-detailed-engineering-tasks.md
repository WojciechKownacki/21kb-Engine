# Sprint 43 · Physics — Detailed Engineering Tasks

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Close concrete physics backend and high-level simulation gaps with stable component contracts, deterministic boundaries, representative scenes, backend-accurate debugging, and performance verification.

## Collider Shapes & Geometry

- [ ] Implement a triangle-mesh static collider that bakes a collider component's mesh reference into a Jolt mesh shape with indexed triangles so arbitrary static level geometry generates contacts, verified by a sphere resting on a concave imported mesh floor.
- [ ] Implement a convex-hull dynamic collider that builds a Jolt convex-hull shape from a point cloud or source mesh vertices with vertex capping and simplification, verified by stacking two hull-shaped bodies that settle without interpenetration.
- [ ] Implement a heightfield terrain collider backed by a Jolt heightfield shape fed from a height sample grid, verified by a raycast and a rolling sphere tracking the sampled terrain profile.
- [ ] Add cylinder and tapered-capsule shape kinds mapping to the corresponding Jolt shapes, verified by creating each and confirming a body with the expected local bounds.
- [ ] Implement multi-shape compound colliders by allowing an entity to own several child collider descriptors combined into a Jolt compound shape, verified by a single body whose L-shaped compound blocks a ray through the concave notch.
- [ ] Rotate the collider center offset by the body rotation when composing the body position in body creation and debug draw so an off-center collider on a rotated entity is simulated at the correct world location, verified by a rotated box whose off-center shape contacts match its rendered wireframe.
- [ ] Implement per-shape convex-radius and margin configuration on the collider component mapping to Jolt's convex radius, verified by a thin box that no longer jitters when configured with a reduced margin.

## Rigid-Body Dynamics

- [ ] Add linear-damping and angular-damping fields to the rigidbody component wired to the body creation settings, verified by a spinning body whose angular velocity decays to a measured fraction over a fixed number of steps.
- [ ] Add per-axis position and rotation freeze flags mapped to individual allowed-degrees-of-freedom bits, replacing the all-or-nothing rotation lock, verified by a body that translates only on one axis while all rotation is locked.
- [ ] Add a center-of-mass override applied via the mass-properties override with a shifted center of mass, verified by a box that tips predictably around its overridden center of mass.
- [ ] Add an inertia-tensor and mass-distribution override so authored mass properties bypass automatic inertia calculation, verified by comparing angular response of a body with custom versus auto inertia under identical torque.
- [ ] Add configurable maximum linear and angular velocity clamps mapped to the body's velocity caps, verified by an impulse that leaves the body at exactly the configured cap.
- [ ] Implement a scene-gravity get and set API forwarding to the physics system, replacing the hardcoded gravity vector, verified by scripting zero gravity and observing a released body remain stationary.
- [ ] Add per-body sleep-threshold and allow-sleep controls mapped to the body and system sleep settings, verified by a never-sleep body staying active indefinitely under a tiny sustained force.
- [ ] Call the broad-phase optimization routine after a bulk static-body insertion batch so large static scenes get a balanced broad-phase tree, verified by a broad-phase quality assertion after adding thousands of static colliders.

## Raycasts & Scene Queries

- [ ] Route the raycast and raycast-all APIs through the Jolt narrow-phase cast-ray against real body shapes, replacing the current engine-geometry AABB and two-sphere approximation, so oriented boxes and true capsules are hit exactly, verified by a ray that misses a rotated box's AABB corner but correctly reports no hit on the real oriented shape.
- [ ] Add oriented shape casts by threading a rotation quaternion into the shape-cast and overlap queries instead of an identity basis, verified by a box-cast that only fits through a diagonal gap when rotated forty-five degrees.
- [ ] Return surface material, sub-shape id, and triangle or face index in the cast result from ray and shape casts so mesh-collider hits report which triangle and material was struck, verified by a ray into a two-material mesh reporting the correct material per face.
- [ ] Implement a single-target ray and shape cast that tests only one specified entity's body, verified by a ray that ignores an occluder and reports the intended body's hit.
- [ ] Implement a point-containment query that reports every body whose shape encloses a world point, verified by a point inside a box returning that box and a point just outside returning nothing.

## Joints & Constraints

- [ ] Implement a motorized and driven joint mode for the hinge and a new slider/prismatic constraint exposing target position, target velocity, and maximum motor force, verified by a hinge driven to and holding a commanded angle under gravity.
- [ ] Implement a spring and soft-constraint mode with frequency and damping on the distance constraint and a new spring joint type, verified by a suspended body oscillating at the configured frequency before settling.
- [ ] Implement a breakable-joint option with a break-force and break-torque threshold that removes the constraint and emits a break event when the applied impulse exceeds the limit, verified by a weight heavy enough to snap a fixed joint and fire the event.
- [ ] Implement a cone-twist swing-twist constraint with swing and twist angle limits for ragdoll-style joints, verified by a limb pinned so its swing stays within the configured cone.
- [ ] Implement a six-degrees-of-freedom configurable constraint exposing per-axis free, locked, and limited translation and rotation, verified by a body constrained to a one-axis rail with all other axes locked.
- [ ] Add a per-joint enable-collision-between-linked-bodies toggle, verified by two jointed bodies that either overlap freely or block each other according to the flag.
- [ ] Report live constraint reaction force and torque through the backend so gameplay can read joint stress, verified by a loaded joint reporting a reaction magnitude proportional to the hung mass.

## Contact & Trigger Events

- [ ] Include per-contact impulse magnitude and relative velocity in the collision event read from the accumulated manifold impulse so gameplay can scale impact effects, verified by a hard landing reporting a larger impulse than a gentle one.
- [ ] Emit all manifold contact points or an averaged contact patch in the collision event instead of only the first point, verified by a face-to-face box landing reporting multiple distinct contact points.
- [ ] Implement a contact-modification callback path that lets gameplay override per-contact friction and restitution or cancel a contact, verified by a one-way platform that passes upward but blocks downward via a cancelled contact.
- [ ] Enable sensor-versus-static and sensor-versus-kinematic detection so a trigger volume detects a static collider entering it, verified by a trigger firing enter against a moved static body.
- [ ] Emit body activation and deactivation wake and sleep events to script through a new pending-event channel, verified by a settling body producing exactly one sleep event.

## Character Controller

- [ ] Implement character crouch and shape-swap that live-switches the character shape with a penetration test before standing, verified by a character unable to stand under a low ceiling remaining crouched.
- [ ] Wire character-to-dynamic-body pushing by applying impulses to bodies the character reports as active contacts, verified by a walking character shoving a light dynamic crate along the floor.
- [ ] Implement character-versus-character collision by registering each character controller with the others, verified by two controllers unable to occupy the same space.
- [ ] Add stick-to-ground and walk-down-stairs handling so a descending character hugs downward slopes instead of launching off ledges, verified by a character keeping grounded state while walking down a staircase.
- [ ] Add a maximum-push-force limit and controlled slope-slide behavior so a character on a too-steep slope slides down at a controlled rate, verified by the steep-ground state producing downhill motion.

## Physics Materials

- [ ] Add friction-combine and restitution-combine modes (average, min, max, multiply) resolved between two contacting colliders via a contact callback, verified by a min-combine pair producing the lower of two friction values in the resulting deceleration.
- [ ] Implement a reusable physics-material asset carrying friction, restitution, combine modes, and a surface tag, referenced by colliders and loaded through the asset manager, verified by two colliders sharing one material asset that hot-reloads to change both.
- [ ] Implement per-triangle material assignment for mesh colliders via a material list on the mesh shape, verified by a raycast onto different triangles of one mesh returning different surface tags.

## Layers & Filtering

- [ ] Implement per-body ignore-pair collision filtering via a group and sub-group filter so two specific entities can ignore each other independent of layers, verified by two same-layer bodies passing through each other while still colliding with a third.

## Determinism, Networking & Async

- [ ] Implement physics state save and restore using Jolt's state serialization exposed as serialize and deserialize on the backend for rollback-netcode snapshots, verified by simulating, snapshotting, diverging, restoring, and reproducing bit-identical body poses.
- [ ] Add a deterministic-simulation mode that fixes worker-thread count and sorts body creation and iteration by stable entity id so runs reproduce across machines, verified by two runs with identical inputs producing identical final poses.
- [ ] Implement asynchronous physics stepping that launches the simulation update on the job system and reads results the following frame with interpolation so the main thread does not block on simulation, verified by main-thread step time dropping below the physics solve time under heavy body counts.

## Debug Visualization & Profiling

- [ ] Implement a Jolt debug-renderer backend that draws real simulated shapes, contact points, and constraint frames, replacing the hand-rolled wireframe approximations, verified by mesh and convex colliders rendering their actual triangles.
- [ ] Add velocity, sleeping-state, and center-of-mass overlays to physics debug draw, verified by a moving body showing a velocity vector that shrinks to zero and recolors when it sleeps.
- [ ] Expose per-step physics profiling stats (active body count, contact-constraint count, island count, solve time), verified by the active-body stat dropping as bodies fall asleep.

## High-Level Physics Systems

- [ ] Implement a wheeled-vehicle system backed by Jolt's vehicle constraint driven by throttle, brake, and steer inputs with per-wheel suspension and friction, verified by a four-wheel vehicle accelerating, steering, and settling on its suspension.
- [ ] Implement a ragdoll system that builds a Jolt ragdoll from a skeleton with per-bone shapes and swing-twist constraints and drives skinned-mesh bone transforms from the simulated pose, verified by a character collapsing into a stable, non-exploding ragdoll on death.
- [ ] Implement kinematic-to-ragdoll blending that pose-matches ragdoll bodies to an animated pose via motor-driven constraints, verified by a partially-driven ragdoll tracking an animation while reacting to an external shove.
- [ ] Implement buoyancy and water volumes that apply a buoyancy impulse with configurable fluid density and drag to bodies inside a marked region, verified by a low-density box floating at a stable waterline and a dense one sinking.
- [ ] Implement a soft-body and cloth system backed by Jolt's soft-body settings producing a simulated deformable mesh, verified by a pinned cloth draping over a sphere and coming to rest.
- [ ] Implement directional and radial force-field volumes for wind, explosions, and attractors that accumulate forces on overlapping bodies each fixed step via an overlap query plus force application, verified by an explosion field launching nearby dynamic bodies radially outward with distance falloff.

## 2D Physics (Box2D)

- [ ] Implement a Box2D-backed 2D physics scene system parallel to the Jolt path, mapping 2D rigidbody and collider components to Box2D bodies and fixtures and stepping a Box2D world, verified by a 2D box falling and resting on a 2D static ground segment.
- [ ] Implement 2D shape casts, overlaps, and contact-event routing for the Box2D backend mirroring the existing physics-backend query and event contract, verified by a 2D ray reporting the nearest fixture and a 2D trigger firing enter and exit.
