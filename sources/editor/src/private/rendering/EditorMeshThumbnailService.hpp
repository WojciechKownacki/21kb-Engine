#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "rendering/EditorMeshPreviewTypes.hpp"

#include <cstdint>

namespace kb::assets {
class AssetManager;
}

namespace kb::editor {

class EditorMeshThumbnailService {
public:
    [[nodiscard]] const EditorMeshThumbnailImage* ThumbnailFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* ThumbnailFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings);
    [[nodiscard]] const EditorMeshThumbnailStats* StatsFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailStats* StatsFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    void Clear() noexcept;
};

[[nodiscard]] EditorMeshThumbnailService& EditorMeshThumbnailCache();

} // namespace kb::editor
