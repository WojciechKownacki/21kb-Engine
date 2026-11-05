#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kParticleGeometryGuide = R"guide(# Particle geometry collisions

Use the existing ParticleEffect asset, module list and ParticlePlayback API.
`CollisionGeometry` supports three CPU collision sources, independently of
camera visibility. CPU particles still draw through the normal GPU renderer.

```cpp
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/particles/ParticlePlayback.hpp"
kb::scene::ParticleCollisionGeometryModule collision;
collision.kind = kb::scene::ParticleCollisionGeometryKind::PhysicsWorld;
collision.radius = 0.07F;
collision.restitution = 0.6F;
collision.friction = 0.1F;
effect.emitters[0].modules.push_back({
    .moduleId = 100, .authoringOrder = 0,
    .type = kb::scene::ParticleModuleType::CollisionGeometry,
    .payload = collision});
if (!kb::scene::ParticleEffectAssetIO::Save(path, effect))
    throw std::runtime_error("invalid particle effect or save failure");
// After project asset discovery, instantiate the saved effect:
auto instance = kb::particles::ParticlePlayback::Create(scene, effectAssetId, owner);
if (!instance.Succeeded()) throw std::runtime_error("particle create failed");
auto playing = kb::particles::ParticlePlayback::Play(scene, instance.instanceId);
```

PhysicsWorld sweeps a sphere of the authored radius through the active physics
provider's synchronized colliders, including its triangle geometry. layerMask
selects physics layers. Initial overlaps use the existing overlap/closest-point
queries. A missing provider rejects creation. This source cannot prewarm before
scene geometry is synchronized. It does not feed particle impulses into bodies.

SignedDistanceGrid contains world-space signed distances in metres, negative
inside. origin is sample (0,0,0); cellSize is positive sample spacing. Set width,
height and depth to at least 2. Samples use index x + width*(y + height*z).
Distances use trilinear interpolation and its gradient for contact normals.
Author a padded, valid distance field; geometry outside the grid is absent.

Heightfield uses width/depth >=2 and height=1. Samples are relative heights above
origin.y, indexed x + width*z. X/Z use cellSize.x/z. Bilinear interpolation and
surface derivatives provide height and normal; outside the X/Z footprint there
is no collision. This is a surface with solid space below it, not a closed volume.
Both grid sources are axis-aligned in world space and accept up to 32768 finite
samples (128 samples per axis). Inline grids also share the existing 512 KiB
source-effect limit. The sampled segment uses quarter-cell spacing,
capped at 256 checks per fixed step: resolve thin features in the data and keep
per-step travel below 64 times the smallest cell spacing. They are not a general
continuous mesh collision solver. PhysicsWorld uses the provider's swept query.

All sources share radius, restitution/friction in [0,1], killOnContact and
maxEventsPerStep with the existing collision/event stage. Collision event
bindings may name the module id. Local emitters already transform their live
particles into world axes; the collision data remains anchored to the world.

Author through the public asset or CLI:

```
kb_cli vfx-module-add --project . --effect /Game/Vfx/Sparks.kbvfx --emitter 1 --type CollisionGeometry
kb_cli vfx-set --project . --effect /Game/Vfx/Sparks.kbvfx --set effect.emitter[0].module[0].payload.radius=0.07
```

The text payload records kind, radius, restitution, friction, maxEventsPerStep,
optional killOnContact, then layerMask for PhysicsWorld. Grids instead record
origin, cellSize, width/height/depth, sampleCount and sample[0] through sample[N-1].
Unknown/inapplicable keys, malformed counts, invalid values and an insufficient
sample array fail validation; failed CLI edits preserve the original file.
Source and versioned compiled artifacts retain the same data through cook.

GpuVisualPreferred selects CPU with snapshot/diagnostic reason
`geometry-collision`. GpuVisualRequired is rejected for an enabled geometry
collision module. This is an explicit backend boundary, not GPU collision with
SDF textures. Screen depth and authored planes retain their existing paths.

Lua uses the same asset/backend:

```lua
local instance = assert(Particles.Create("/Game/Vfx/Sparks.kbvfx", {entity=self.entity}))
assert(Particles.Play(instance))
```

Declare leading `# @asset` dependencies in native descriptors and `-- @asset`
dependencies in Lua when loading assets dynamically. Native authoring remains
the primary interface. Windows GPU rendering and source-free package acceptance
are covered by `kb_editor_particle_geometry_package`; other hosts are unqualified.
)guide";
}
