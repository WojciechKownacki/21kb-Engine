#pragma once

#include <cstddef>
#include <cstdint>

namespace kb::render {

enum class MeshPassType : std::uint8_t {
    Depth,
    BaseOpaque,
    GBuffer,
    BaseTransparent,
    ShadowDepth,
    MotionVectors,
    SelectionId,
    EditorSelection,
    Gizmo,
    // A material's outline: the object drawn again as a hull pushed out along its normals (the front faces culled), after
    // the opaque scene, in the colour and width the material names.
    Outline,
    // The part of an object that lies behind other surfaces, drawn in the material's hidden colour (a silhouette seen
    // through a wall).
    OutlineHidden,
};

inline constexpr std::size_t kMeshPassTypeCount = static_cast<std::size_t>(MeshPassType::OutlineHidden) + 1U;

} // namespace kb::render
