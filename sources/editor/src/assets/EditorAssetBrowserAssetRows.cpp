#include "assets/EditorAssetBrowserAssetRows.hpp"

#include "assets/EditorAssetBrowserPathUtils.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::string_view kMaterialTypeFilter = "Materials";

[[nodiscard]] bool IsMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance";
}

[[nodiscard]] bool IsMaterialGraphAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterialGraph" || metadata.type == "MaterialGraph";
}

[[nodiscard]] bool IsMaterialTypeAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterialType" || metadata.type == "MaterialType";
}

[[nodiscard]] bool IsMaterialFamilyAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return IsMaterialAsset(metadata) || IsMaterialGraphAsset(metadata) || IsMaterialTypeAsset(metadata);
}

[[nodiscard]] bool MatchesFolder(const std::filesystem::path& assetPath, const std::filesystem::path& selectedFolder, bool recursive) {
    if (recursive) {
        return asset_browser::StartsWithFolder(assetPath, selectedFolder);
    }
    return asset_browser::Normalize(asset_browser::ParentVirtualPath(assetPath)) == asset_browser::Normalize(selectedFolder);
}

[[nodiscard]] std::string SearchText(const kb::assets::AssetMetadata& metadata) {
    std::string text = metadata.name + " " + metadata.type + " " + metadata.importCategory + " " + asset_browser::Normalize(metadata.virtualPath);
    if (IsMaterialAsset(metadata)) {
        text += " material materials materialy pbr shader surface .kbmat";
        if (metadata.type == "RenderMaterialInstance") {
            text += " instance material-instance .kbmatinst";
        }
    }
    if (IsMaterialGraphAsset(metadata)) {
        text += " material graph shader nodes materialy graf .kbmaterialgraph";
    }
    if (IsMaterialTypeAsset(metadata)) {
        text += " material type schema shader contract materialy typ .kbmaterialtype";
    }
    return asset_browser::Lower(std::move(text));
}

[[nodiscard]] bool MatchesSearch(const kb::assets::AssetMetadata& metadata, std::string_view searchQuery) {
    if (searchQuery.empty()) {
        return true;
    }

    const std::string query = asset_browser::Lower(std::string{ searchQuery });
    return SearchText(metadata).find(query) != std::string::npos;
}

[[nodiscard]] bool MatchesType(const kb::assets::AssetMetadata& metadata, std::string_view typeFilter) {
    return typeFilter.empty()
        || metadata.type == typeFilter
        || (typeFilter == kMaterialTypeFilter && IsMaterialFamilyAsset(metadata));
}

[[nodiscard]] bool MatchesTemplateFilter(const kb::assets::AssetMetadata& metadata, bool showTemplates) {
    return showTemplates || metadata.type != "ScenePrefab";
}

} // namespace

std::vector<EditorAssetItemRow> EditorAssetBrowserAssetRows::Build(
    const kb::assets::AssetManager& manager,
    const std::filesystem::path& selectedFolder,
    kb::assets::AssetId selectedAsset,
    bool recursive,
    std::string_view searchQuery,
    std::string_view typeFilter,
    bool showTemplates,
    EditorAssetSortMode sortMode) {
    std::vector<EditorAssetItemRow> rows;
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (!MatchesFolder(metadata.virtualPath, selectedFolder, recursive)
            || !MatchesSearch(metadata, searchQuery)
            || !MatchesType(metadata, typeFilter)
            || !MatchesTemplateFilter(metadata, showTemplates)) {
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
    bool hasMaterialAsset = false;
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (!metadata.type.empty()) {
            types.insert(metadata.type);
        }
        hasMaterialAsset = hasMaterialAsset || IsMaterialFamilyAsset(metadata);
    }
    if (hasMaterialAsset) {
        types.insert(std::string{ kMaterialTypeFilter });
    }
    return { types.begin(), types.end() };
}

} // namespace kb::editor
