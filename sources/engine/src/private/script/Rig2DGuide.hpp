#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kRig2DGuide=R"guide(# Weighted skeletal animation in 2D

Rig2D is planar XY authoring over the shared skeleton, weighted mesh and Animator
runtime. It deforms triangles, rather than replacing sprite frames. Owners can
be placed in a 3D scene with ordinary transforms.

`kb_cli rig2d new --project <dir> --name Ribbon` creates a `.kbrig2d` source
with a two-bone weighted ribbon and a keyed bend. `rig2d show --rig <path>`
prints the complete JSON. Edit that file, or use `rig2d set --rig <path>
--data <complete object>`; invalid edits leave the saved source unchanged.
`rig2d build --rig <path>` validates and writes the sibling folder containing
Skeleton.kbskeleton, Mesh.kbskeletalmesh, Motion.kbanim and
Controller.kbanimcontroller. The controller has layer Base and state Play.
Rebuild after editing the source. Save failures are explicit; bundle files are
saved individually using the canonical asset IO.

```cpp
#include "engine/scene/Rig2DAsset.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/SceneAnimators.hpp"
// DecodeRig2D or construct the typed Rig2DAsset: bones, weighted vertices,
// triangle indices, duration, looping and per-bone local-pose key tracks.
kb::scene::CompiledRig2D compiled;
std::string error;
if (!kb::scene::CompileRig2D(source, skeletonAssetId, materialAssetId,
        "/Game/Rigs/Ribbon/Motion.kbanim", compiled, error))
    throw std::runtime_error(error);
// Save the four compiled members with SkeletonAssetIO, SkeletalMeshAssetIO
// and AnimationAssetIO. Skeleton identity is the registered virtual path id.
if (!kb::scene::AttachRig2D(scene, owner,
        "/Game/Rigs/Ribbon/Mesh.kbskeletalmesh",
        "/Game/Rigs/Ribbon/Controller.kbanimcontroller", error))
    throw std::runtime_error(error);
// Once the shared animator is initialized:
if (!scene.Animators().Play(owner, "Base", "Play"))
    throw std::runtime_error("Animator is not ready");
```

```lua
-- @asset /Game/Rigs/Ribbon/Mesh.kbskeletalmesh
-- @asset /Game/Rigs/Ribbon/Controller.kbanimcontroller
function Ready(self)
  assert(Rig2D.Attach(self.entity, "/Game/Rigs/Ribbon/Mesh.kbskeletalmesh",
    "/Game/Rigs/Ribbon/Controller.kbanimcontroller"))
end
-- On subsequent ticks use Animator.Play/SetSpeed/CrossFade with that entity.
-- CallFunction("Animator.Play", {entity=self.entity,layer="Base",state="Play",
--   normalizedTime=0.5}) also selects and seeks the shared clip.
```

Bind poses and keys contain position [x,y], rotation in degrees about Z, and
positive scale [x,y]. Parents precede their children; bone array order defines
stable IDs starting at one. Vertex bones/weights each contain four entries;
all indices are valid, even for zero weights, and weights sum to one. Positions
are mesh-local bind positions; UVs use the normal material path. Indices are
triangle triples, normalized to face local -Z. Put textured materials on the
ordinary deformed-geometry material slots; no separate 2D material system exists.

Limits: 128 bones, 32768 vertices, 196608 indices, one generated clip with up to
128 tracks and 16384 keys (4096 per track), and 16 MiB source JSON. Clip duration
is positive and at most one hour. Local scale is 0.001..100. This authoring
frontend does not import external rig formats, auto-weight meshes or provide a
bone-paint GUI. Existing Animator controllers can add further clips, blends,
events and transitions. Source rigs are authoring inputs; scenes and scripts
reference the compiled standard assets for cook/runtime. Windows D3D11 is the
qualified editor/player path.
)guide";
}
