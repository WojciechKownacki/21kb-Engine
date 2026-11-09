#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kCsgGuide=R"guide(# CSG boolean blockout

`engine/scene/CsgAsset.hpp` is the authoring API. `CsgAsset::brushes` is an ordered
left fold: first union brush seeds the solid; later union/subtract/intersect
brushes modify it. Boxes use positive metre size. Mesh brushes contain a closed,
consistently outward-wound triangle mesh. Each brush applies positive scale,
Euler XYZ rotation in degrees, then translation in metres. CompileCsg uses the
serial Manifold solid boolean backend; it validates topology and returns ordinary
positions, triangle indices and volume. Failed compilation preserves output.

```cpp
kb::scene::CsgAsset source;
source.brushes.push_back({.size={2,2,1}});
source.brushes.push_back({.operation=kb::scene::CsgOperation::Subtract,
                         .size={1,1,2},.rotationDegrees={0,0,25}});
kb::scene::CompiledCsg compiled;
std::string error;
if (!kb::scene::CompileCsg(source,compiled,error)) throw std::runtime_error(error);
// WriteCsgObj emits source for the existing mesh importer; WriteCollisionMesh
// saves compiled.mesh. CLI csg build performs both steps using the same backend.
```

`csg new --project <dir> --name Blockout --data-file recipe.json` saves a `.kbcsg`
source. `csg show --project <dir> --source /Game/Geometry/Blockout.kbcsg` reads it.
`csg set` accepts a complete strict schema-1 object through --data or --data-file.
Each brush has operation, kind (`box` or `mesh`), position, rotation and scale;
box also has size; mesh instead has positions (xyz arrays) and indices.
`csg build --project <dir> --source /Game/Geometry/Blockout.kbcsg
--output /Game/Compiled/Blockout` imports Geometry.21kb and writes
Geometry.kbcollision. Rebuild explicitly after changing the recipe. Import
conflicts are reported through the existing importer. Generated OBJ stays in
Saved/Csg; cook uses ordinary imported mesh and collision, without rebuilding CSG.

`AttachCompiledCsg(scene,entity,"/Game/Compiled/Blockout/Geometry.21kb",
"/Game/Compiled/Blockout/Geometry.kbcollision",error)` sets persistent ordinary
MeshRenderer, MeshCollider and static Rigidbody components after validating asset
references. Existing material assignment survives. Lua `CSG.Attach(self.entity,
"...Geometry.21kb","...Geometry.kbcollision")` calls this backend;
Lua can attach baked geometry but does not compile recipes. Add both assets to
native script @asset references when attaching them dynamically.

Limits: 64 brushes, 65536 total input/output positions, 393216 triangle indices,
16 MiB source. Empty results, non-manifold inputs, nonpositive scale and dynamic
triangle bodies and system-owned transient renderers fail explicitly. Output is a hard-normal static mesh with planar
metre UVs; there is no editable-face GUI, material provenance, UV unwrapping or
live runtime boolean mutation. Split larger blockouts into separate assets.
)guide";
}
