#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "rendering/EditorMeshPreviewTypes.hpp"

namespace kb::editor {

class EditorMeshThumbnailDiskCache {
public:
    EditorMeshThumbnailDiskCache() = delete;

    [[nodiscard]] static bool Load(
        const kb::assets::AssetMetadata& metadata,
        EditorMeshThumbnailImage& thumbnail,
        EditorMeshThumbnailImage& preview,
        EditorMeshThumbnailStats& stats);
    static void Save(
        const kb::assets::AssetMetadata& metadata,
        const EditorMeshThumbnailImage& thumbnail,
        const EditorMeshThumbnailImage& preview,
        const EditorMeshThumbnailStats& stats);
};

} // namespace kb::editor
