#include "scene/input/EditorInputAssetCatalog.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <algorithm>
#include <iterator>

namespace kb::editor {

EditorInputAssetCatalog::EditorInputAssetCatalog(const kb::scene::Scene& scene) noexcept
    : scene_(scene) {}

std::vector<std::uint64_t> EditorInputAssetCatalog::SortedIdsOfType(std::string_view type) const {
    std::vector<std::uint64_t> ids;
    for (const kb::assets::AssetMetadata& metadata : scene_.Assets().Manager().Registry().All()) {
        if (metadata.type == type) {
            ids.push_back(metadata.id.value);
        }
    }
    std::ranges::sort(ids);
    return ids;
}

std::uint64_t EditorInputAssetCatalog::NextCyclicId(const std::vector<std::uint64_t>& ids, std::uint64_t current) {
    if (ids.empty()) {
        return current;
    }
    const auto found = std::ranges::find(ids, current);
    if (found == ids.end()) {
        return ids.front();
    }
    const std::size_t index = static_cast<std::size_t>(std::distance(ids.begin(), found));
    return ids[(index + 1U) % ids.size()];
}

} // namespace kb::editor
