#pragma once

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

namespace kb::editor {

class EditorMaterialPreviewMeshFactory {
public:
    EditorMaterialPreviewMeshFactory() = delete;

    [[nodiscard]] static kb::render::RenderMeshAssetData BuildSphere();
    [[nodiscard]] static kb::render::RenderMeshAssetData BuildCube();
    [[nodiscard]] static kb::render::RenderMeshAssetData BuildPlane();
};

} // namespace kb::editor
