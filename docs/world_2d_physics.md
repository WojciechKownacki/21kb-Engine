# 2D physics (Box2D)

The World 2D plugin (`World.Map2D`, on by default) simulates 2D physics with the vendored Box2D (MIT, `third_party/box2d`). Three
scene components describe it; the simulation runs while the scene is playing and never while editing.

| Component | What it is |
|---|---|
| **Body 2D** | what moves: `type` (0 static, 1 kinematic, 2 dynamic), `gravityScale`, `linearDamping`, `angularDamping`, `fixedRotation`, `bullet`, `allowSleep`, `sideView`, `startVelocityX`, `startVelocityY`, `enabled` |
| **Collider 2D** | a shape: `shape` (0 box, 1 circle, 2 capsule), `width`, `height`, `radius`, `offsetX`, `offsetY`, `angleDegrees`, `isTrigger`, `density`, `friction`, `restitution`, `layer` (0 to 15), `collidesWith` (mask of layers), `sideView`, `enabled` |
| **Character Controller 2D** | a walking character: `radius`, `height`, `gravityScale`, `useGravity`, `slopeLimitDegrees`, `stepHeight`, `layer`, `collidesWith`, `sideView`, `enabled` |

A Collider 2D belongs to the Body 2D on its own object or on the nearest parent; with no body it is a static shape. A body without a
collider has no shape. A character is its own kinematic body: do not add a Body 2D to it.

## Planes

`sideView` true (the default) simulates in the X and Y plane with gravity down; `sideView` false simulates in the X and Z plane with no
gravity, for a top-down game. The two planes are separate worlds: a shape only meets shapes of its own plane. In a top-down body, the
second coordinate of every 2D call (velocity, ray, impulse) is Z.

## Layers

Every shape has a `layer` (0 to 15) and a mask `collidesWith`. Two shapes collide only when each one's mask contains the other's
layer. Ray and overlap queries take a mask of layers too.

## Events

Contacts and trigger overlaps reach scripts as the same `OnCollisionEnter/Exit` and `OnTriggerEnter/Exit` events the 3D physics sends
(entity, other entity, point and normal in the plane). A shape with `isTrigger` detects but does not block.

## Scripts

`Physics2D.SetVelocity`, `GetVelocity`, `AddImpulse`, `AddForce` (dynamic bodies), `Raycast` (hit, entity, point, normal, distance;
triggers are skipped), `OverlapCircle`, `OverlapBox` (count and first entity), `MoveCharacter` (walk velocity along the ground),
`JumpCharacter` (upward speed, only while standing), `IsGrounded` and the joint calls below. Calls refuse non-finite numbers and entities that are not in
the simulation, with the reason. `Body2D`, `Collider2D` and `CharacterController2D` are also available through the component API.

## Joints

`Physics2D.CreateJoint` joins two bodies as they are now, or one body to the world (leave `second` out). `type` is `distance` (a rope or
a spring between the points `x`, `y` and `x2`, `y2`), `revolute` (a hinge at `x`, `y`), `prismatic` (a slider along `axisX`, `axisY`) or
`weld` (rigid). Optional: `collideConnected`, `enableLimit` with `lower` and `upper` (degrees for a hinge, units otherwise),
`enableMotor` with `motorSpeed` and `maxMotor`, `hertz` and `dampingRatio` for a spring, and `length` for a rope. The call answers the
joint number. `Physics2D.SetJointMotor` drives a hinge or a slider, `Physics2D.DestroyJoint` removes a joint and `Physics2D.JointExists`
asks. A joint ends when one of its bodies goes away. Bodies in different planes, two equal bodies, bodies that are not in the
simulation, joints of two non-dynamic bodies and non-finite numbers are refused with the reason. Joints are made by scripts while
playing and are not saved with the scene.

## Characters

A character slides along shapes, climbs slopes up to `slopeLimitDegrees`, steps over ledges up to `stepHeight`, falls with gravity and
jumps with `JumpCharacter`. It is also a body for the others: dynamic shapes are pushed away by it (it is not pushed), and triggers
see it.

## Tilemaps

The collision of a Tilemap with an enabled Tilemap Collider is also made into static shapes of the plane of its map (a wall map is a
side view, a flat map is top down). Solid cells, slopes and polygons, one-way ledges and trigger cells become shapes; neighbouring
equal cells are joined. The shapes follow the map when a cell is set or erased while playing. Only the ground floor (floor 0) is used,
and the map object's position is followed (not its rotation or scale). A one-way ledge stops dynamic bodies and characters that land
on it from above and lets them pass from below or the side; it is ignored in a top-down map. Shapes of a map have layer 0 and belong
to the map object, so events name it as the other object.

## Limits

No Joint 2D component yet (joints are made by scripts), no per-tile layers, no dropping through a ledge on request. The simulation uses the scene's fixed step.

## Tests

`kb_world2d_physics_tests` covers falling, resting, kinematic and static bodies, impulses, layers and masks, trigger and contact
events, ray and overlap queries, the top-down plane, child colliders, characters (landing, walking, walls, jumps, slopes, ledges),
joints (hinge, rope, slider, weld, motor), tilemap shapes (floor, walls, slopes, ledges, triggers, editing while playing), the scene file and the shared component table.
