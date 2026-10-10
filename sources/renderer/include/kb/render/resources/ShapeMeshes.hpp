#pragma once

#include "engine/assets/BuiltInShapes.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <string_view>

namespace kb::render {

// The one generator of primitive geometry: the engine's built-in shapes (/Engine/Shapes, sized like
// Unity's primitives) and the editor's material/animation preview meshes are both built here.
// Left-handed, Y up; a triangle's front face is the side its Cross(b - a, c - a) normal points to.
class ShapeMeshes {
public:
    ShapeMeshes() = delete;

    [[nodiscard]] static RenderMeshAssetData BuiltIn(kb::assets::BuiltInShape shape);

    [[nodiscard]] static RenderMeshAssetData Box(float halfExtent, std::string_view materialName);
    [[nodiscard]] static RenderMeshAssetData Sphere(float radius, std::string_view materialName);
    [[nodiscard]] static RenderMeshAssetData Capsule(float radius, float height, std::string_view materialName);
    [[nodiscard]] static RenderMeshAssetData Cylinder(float radius, float height, std::string_view materialName);
    [[nodiscard]] static RenderMeshAssetData Cone(float radius, float height, std::string_view materialName);
    // Square of side `size` on XZ facing +Y, cut into `divisions` x `divisions` cells.
    [[nodiscard]] static RenderMeshAssetData Ground(float size, int divisions, std::string_view materialName);
    // Square of side `size` on XY; facesNegativeZ is the side a camera looking down +Z sees.
    [[nodiscard]] static RenderMeshAssetData Quad(float size, bool facesNegativeZ, std::string_view materialName);
};

} // namespace kb::render
