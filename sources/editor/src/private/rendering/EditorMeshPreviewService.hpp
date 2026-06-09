#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "rendering/EditorMeshPreviewTypes.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace kb::editor {

class EditorMeshPreviewService {
public:
    [[nodiscard]] const EditorMeshThumbnailImage* ThumbnailFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings);
    [[nodiscard]] const EditorMeshThumbnailStats* StatsFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshValidationResult* ValidationFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* CachedPreviewFor(const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings) const noexcept;
    [[nodiscard]] const EditorMeshThumbnailStats* CachedStatsFor(const kb::assets::AssetMetadata& metadata) const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    void Clear() noexcept;

private:
    enum class EntryState : std::uint8_t {
        Ready,
        Failed,
    };

    struct Entry {
        struct PreviewVariant {
            EditorMeshPreviewSettings settings{};
            EditorMeshThumbnailImage image{};
        };

        std::uint64_t contentHash = 0;
        EntryState state = EntryState::Failed;
        EditorMeshThumbnailImage thumbnail{};
        EditorMeshThumbnailImage preview{};
        std::vector<PreviewVariant> previewVariants{};
        EditorMeshThumbnailStats stats{};
        EditorMeshPreviewGeometry geometry{};
        EditorMeshValidationResult validation{};
        bool geometryLoaded = false;
    };

    [[nodiscard]] static bool IsMeshAsset(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] static std::optional<Entry> BuildEntry(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] Entry& EnsureEntry(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] bool EnsureGeometry(const kb::assets::AssetMetadata& metadata, Entry& entry);

    std::unordered_map<std::uint64_t, Entry> entries_;
    std::uint64_t revision_ = 1;
};

[[nodiscard]] EditorMeshPreviewService& EditorMeshPreviewCache();

} // namespace kb::editor
