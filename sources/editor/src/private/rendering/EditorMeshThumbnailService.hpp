#pragma once

#include "engine/assets/AssetMetadata.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace kb::editor {

struct EditorMeshThumbnailImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> bgra;
};

class EditorMeshThumbnailService {
public:
    [[nodiscard]] const EditorMeshThumbnailImage* ThumbnailFor(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    void Clear() noexcept;

private:
    enum class EntryState : std::uint8_t {
        Ready,
        Failed,
    };

    struct Entry {
        std::uint64_t contentHash = 0;
        EntryState state = EntryState::Failed;
        EditorMeshThumbnailImage image{};
    };

    [[nodiscard]] static bool IsMeshAsset(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] static std::optional<EditorMeshThumbnailImage> RenderThumbnail(const kb::assets::AssetMetadata& metadata);

    std::unordered_map<std::uint64_t, Entry> entries_;
    std::uint64_t revision_ = 1;
};

} // namespace kb::editor
