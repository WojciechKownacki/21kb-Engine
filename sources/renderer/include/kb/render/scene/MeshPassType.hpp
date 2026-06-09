#pragma once

#include <cstdint>

namespace kb::render {

enum class MeshPassType : std::uint8_t {
    Depth,
    BaseOpaque,
    BaseTransparent,
    ShadowDepth,
    SelectionId,
    EditorSelection,
    Gizmo,
};

} // namespace kb::render
