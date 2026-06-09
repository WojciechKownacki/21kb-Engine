#pragma once

#include "rendering/EditorMeshPreviewTypes.hpp"

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

namespace kb::editor {

class EditorMeshPreviewRasterizer {
public:
    EditorMeshPreviewRasterizer() = delete;

    [[nodiscard]] static EditorMeshPreviewGeometry ExtractGeometry(const kb::render::RenderMeshAssetData& mesh);
    [[nodiscard]] static EditorMeshThumbnailImage Render(
        const kb::render::RenderMeshAssetData& mesh,
        int size,
        const EditorMeshPreviewSettings& settings);
    [[nodiscard]] static EditorMeshThumbnailImage Render(
        const EditorMeshPreviewGeometry& geometry,
        int size,
        const EditorMeshPreviewSettings& settings);
};

} // namespace kb::editor
