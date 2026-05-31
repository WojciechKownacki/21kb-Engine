#include "assets/EditorAssetBrowserAssetRows.hpp"

#include "assets/EditorAssetBrowserPathUtils.hpp"

#include <algorithm>
#include <set>

namespace kb::editor {
namespace {

[[nodiscard]] bool MatchesFolder(const std::filesystem::path& assetPath, const std::filesystem::path& selectedFolder, bool recursive) {
    if (recursive) {
        return asset_browser::StartsWithFolder(assetPath, selectedFolder);
    }
    return asset_browser::Normalize(asset_browser::ParentVirtualPath(assetPath)) == asset_browser::Normalize(selectedFolder);
}

[[nodiscard]] bool MatchesSearch(const kb::assets::AssetMetadata& metadata, std::string_view searchQuery) {
    if (searchQuery.empty()) {
        return true;
    }

    const std::string query = asset_browser::Lower(std::string{ searchQuery });
    return asset_browser::Lower(metadata.name).find(query) != std::string::npos
        || asset_browser::Lower(metadata.type).find(query) != std::string::npos
        || asset_browser::Lower(asset_browser::Normalize(metadata.virtualPath)).find(query) != std::string::npos;
}

[[nodiscard]] bool MatchesType(const kb::assets::AssetMetadata& metadata, std::string_view typeFilter) {
    return typeFilter.empty() || metadata.type == typeFilter;
}

} // namespace

std::vector<EditorAssetItemRow> EditorAssetBrowserAssetRows::Build(
    const kb::assets::AssetManager& manager,
    const std::filesystem::path& selectedFolder,
    kb::assets::AssetId selectedAsset,
    bool recursive,
    std::string_view searchQuery,
    std::string_view typeFilter,
    EditorAssetSortMode sortMode) {
    std::vector<EditorAssetItemRow> rows;
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (!MatchesFolder(metadata.virtualPath, selectedFolder, recursive) || !MatchesSearch(metadata, searchQuery) || !MatchesType(metadata, typeFilter)) {
            continue;
        }

        rows.push_back(EditorAssetItemRow{
            .metadata = metadata,
            .selected = metadata.id == selectedAsset,
            .loaded = manager.IsLoaded(metadata.id),
        });
    }

    std::ranges::sort(rows, [sortMode](const EditorAssetItemRow& left, const EditorAssetItemRow& right) {
        switch (sortMode) {
        case EditorAssetSortMode::Type:
            if (left.metadata.type != right.metadata.type) {
                return left.metadata.type < right.metadata.type;
            }
            return left.metadata.name < right.metadata.name;
        case EditorAssetSortMode::Path:
            return asset_browser::Normalize(left.metadata.virtualPath) < asset_browser::Normalize(right.metadata.virtualPath);
        case EditorAssetSortMode::Name:
        default:
            if (left.metadata.name != right.metadata.name) {
                return left.metadata.name < right.metadata.name;
            }
            return asset_browser::Normalize(left.metadata.virtualPath) < asset_browser::Normalize(right.metadata.virtualPath);
        }
    });
    return rows;
}

std::vector<std::string> EditorAssetBrowserAssetRows::AssetTypes(const kb::assets::AssetManager& manager) {
    std::set<std::string> types;
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (!metadata.type.empty()) {
            types.insert(metadata.type);
        }
    }
    return { types.begin(), types.end() };
}

} // namespace kb::editor
