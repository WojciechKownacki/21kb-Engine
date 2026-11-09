#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kMap2DGeometryGuide=R"guide(# Map topology and vector geometry

Use `kb_cli map2d new/set --topology rectangular|isometric|hexagonal`.
Orientation independently selects the XZ ground or XY wall plane. Isometric
uses a diamond lattice. Hexagonal uses pointy axial q/r coordinates and six
edge neighbors; diagonal policy must be None. Cell size is the projected width.
Tile art uses the existing alpha atlas: use diamond or hexagonal silhouettes
and the corresponding image aspect ratio. Sprite paint/flood fill, picking,
pathfinding, prefab placement and intrinsic solid colliders share this topology.
Rectangular ground-border brushes and platformer path mode are rejected on hex
and nonrectangular maps respectively.

```cpp
#include "engine/assets/Map2DAsset.hpp"
#include "engine/assets/Map2DNavigation.hpp"
#include "engine/scene/SceneVectorShapes2D.hpp"
// Map positions are owner-local, before its world transform:
auto center = kb::assets::Map2DCellLocalPoint(map, q, r);
auto neighbors = kb::assets::Map2DNeighbourOffsets(map.topology);
auto path = kb::assets::FindMap2DPathResult(map, {1,1}, {0,2});
std::string error;
if (!kb::scene::SceneVectorShapes2D::Load(scene, owner,
        "/Game/Shapes/Outline.kbvector2d", error)) throw std::runtime_error(error);
if (!kb::scene::SceneVectorShapes2D::SetPoint(scene, owner, 1, {2,1}, error))
    throw std::runtime_error(error);
auto authored = kb::scene::SceneVectorShapes2D::Get(scene, owner);
if (!kb::scene::SaveVectorShape2D(outputPath, *authored, error))
    throw std::runtime_error(error);
```

Create assets with `vector2d-new --project <dir> --name Outline --kind Polygon
--points '[[0,0],[2,0],[1,1],[0,2]]'`. Use kind Line plus `--width 0.1`
and `--closed true` for a closed stroke. `vector2d-set/show --shape <path>`
uses the same validation and atomic save. A material is an optional virtual
RenderMaterial asset path. Polygon outlines are simple, concave or convex,
3..256 points without holes or a repeated closing vertex. Lines have 2..2048
points, butt caps and bevel joins. Points and width use owner-local XY units.
Self-intersections invalidate polygons; invalid edits preserve the old geometry.

```lua
-- @asset /Game/Shapes/Outline.kbvector2d
function Ready(self)
  assert(Vector2D.Load(self.entity, "/Game/Shapes/Outline.kbvector2d"))
  assert(Vector2D.SetPoint(self.entity, 1, 2, 1))
end
-- Map2D.CellToWorld/WorldToCell/Neighbor use the same projection backend.
```

Persist vector source assets and their binding script with the scene. Install
on Ready in C++ or Lua; generated render meshes are transient. Authoring can
save Get() to persist deformed points. Runtime rollback snapshots explicitly
reject installed vector state. Shapes share the dynamic surface budget with
other surface producers; an owner cannot replace another producer's surface.
)guide";
}
