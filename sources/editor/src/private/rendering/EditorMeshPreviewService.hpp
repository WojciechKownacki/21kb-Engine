#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "rendering/EditorMeshPreviewTypes.hpp"

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace kb::assets {
class AssetManager;
}

namespace kb::scene {
struct SkeletalMeshAsset;
}

namespace kb::editor {

class EditorMeshPreviewService {
public:
    [[nodiscard]] const EditorMeshThumbnailImage* ThumbnailFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* ThumbnailFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings);
    [[nodiscard]] const EditorMeshThumbnailStats* StatsFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailStats* StatsFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshValidationResult* ValidationFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshValidationResult* ValidationFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* CachedPreviewFor(const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings) const noexcept;
    [[nodiscard]] const EditorMeshThumbnailStats* CachedStatsFor(const kb::assets::AssetMetadata& metadata) const noexcept;
    [[nodiscard]] const EditorMeshValidationResult* CachedValidationFor(const kb::assets::AssetMetadata& metadata) const noexcept;
    [[nodiscard]] std::size_t PumpCompletedPreviews();
    [[nodiscard]] std::size_t PumpCompletedPreviews(kb::assets::AssetManager& manager);
    [[nodiscard]] bool HasPendingPreviewWork() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    void Clear() noexcept;

private:
    enum class EntryState : std::uint8_t {
        Loading,
        Ready,
        Failed,
    };

    struct Entry {
        struct PreviewVariant {
            EditorMeshPreviewSettings settings{};
            EditorMeshThumbnailImage image{};
        };

        std::uint64_t contentHash = 0;
        std::uint64_t materialContentHash = 0;
        EntryState state = EntryState::Failed;
        EditorMeshThumbnailImage thumbnail{};
        EditorMeshThumbnailImage preview{};
        std::vector<PreviewVariant> previewVariants{};
        EditorMeshThumbnailStats stats{};
        EditorMeshPreviewGeometry geometry{};
        EditorMeshValidationResult validation{};
        bool geometryLoaded = false;
    };

    struct PendingPreview {
        kb::assets::AssetMetadata metadata{};
        std::uint64_t materialContentHash = 0;
        EditorMeshPreviewSettings settings{};
        std::future<EditorMeshThumbnailImage> future{};
    };

    struct PendingEntry {
        kb::assets::AssetMetadata metadata{};
        std::future<std::optional<Entry>> future{};
    };

    [[nodiscard]] static bool IsMeshAsset(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] static std::optional<Entry> BuildEntry(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] static std::optional<Entry> BuildEntry(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshThumbnailImage* PreviewFor(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings);
    [[nodiscard]] const EditorMeshThumbnailStats* StatsFor(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] const EditorMeshValidationResult* ValidationFor(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] Entry& EnsureEntry(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] Entry& EnsureEntry(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] bool EnsureGeometry(const kb::assets::AssetMetadata& metadata, Entry& entry);
    [[nodiscard]] bool EnsureGeometry(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata, Entry& entry);
    [[nodiscard]] bool QueueSkeletalPreview(
        const kb::assets::AssetMetadata& metadata,
        const Entry& entry,
        const EditorMeshPreviewSettings& settings);
    void QueueSkeletalEntryLoad(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata);
    void QueueSkeletalEntryBuild(
        const kb::assets::AssetMetadata& metadata,
        std::shared_ptr<const kb::scene::SkeletalMeshAsset> mesh);
    void StoreCompletedPreview(Entry& entry, const EditorMeshPreviewSettings& settings, EditorMeshThumbnailImage image);

    std::unordered_map<std::uint64_t, Entry> entries_;
    std::vector<PendingEntry> pendingEntries_;
    std::vector<PendingPreview> pendingPreviews_;
    std::uint64_t revision_ = 1;
};

[[nodiscard]] EditorMeshPreviewService& EditorMeshPreviewCache();

} // namespace kb::editor
