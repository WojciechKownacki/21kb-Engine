#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

[[nodiscard]] bool IsPrefabLike(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "ScenePrefab" || Lower(metadata.virtualPath.extension().string()) == ".kbprefab";
}

[[nodiscard]] std::optional<EditorAssetItemRow> AssetRowAt(
    const EditorAssetBrowserHit& hit,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    if (hit.kind != EditorAssetBrowserHitKind::Asset) {
        return std::nullopt;
    }

    const std::vector<EditorAssetItemRow> assets = state.AssetRows(manager);
    if (hit.index >= assets.size()) {
        return std::nullopt;
    }
    return assets[hit.index];
}

[[nodiscard]] std::optional<std::filesystem::path> TreeFolderAt(
    std::size_t index,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    const std::vector<EditorAssetFolderRow> folders = state.FolderRows(manager);
    return index < folders.size() ? std::optional<std::filesystem::path>{ folders[index].virtualPath } : std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> ContentFolderAt(
    std::size_t index,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    const std::vector<EditorAssetFolderRow> folders = state.ChildFolderRows(manager);
    return index < folders.size() ? std::optional<std::filesystem::path>{ folders[index].virtualPath } : std::nullopt;
}

} // namespace

std::optional<std::filesystem::path> EditorAssetBrowserHitPayloadResolver::PrefabAssetAt(
    const EditorAssetBrowserHit& hit,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    const std::optional<EditorAssetItemRow> asset = AssetRowAt(hit, state, manager);
    if (!asset.has_value() || !IsPrefabLike(asset->metadata)) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> physical = manager.Mounts().Resolve(asset->metadata.virtualPath);
    return physical.has_value() ? physical : asset->metadata.physicalPath;
}

std::optional<kb::assets::AssetId> EditorAssetBrowserHitPayloadResolver::AssetIdAt(
    const EditorAssetBrowserHit& hit,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    const std::optional<EditorAssetItemRow> asset = AssetRowAt(hit, state, manager);
    return asset.has_value() ? std::optional<kb::assets::AssetId>{ asset->metadata.id } : std::nullopt;
}

std::optional<kb::assets::AssetMetadata> EditorAssetBrowserHitPayloadResolver::AssetMetadataAt(
    const EditorAssetBrowserHit& hit,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    const std::optional<EditorAssetItemRow> asset = AssetRowAt(hit, state, manager);
    return asset.has_value() ? std::optional<kb::assets::AssetMetadata>{ asset->metadata } : std::nullopt;
}

std::optional<std::filesystem::path> EditorAssetBrowserHitPayloadResolver::FolderAt(
    const EditorAssetBrowserHit& hit,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    switch (hit.kind) {
    case EditorAssetBrowserHitKind::Folder:
        return TreeFolderAt(hit.index, state, manager);
    case EditorAssetBrowserHitKind::ContentFolder:
        return ContentFolderAt(hit.index, state, manager);
    default:
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> EditorAssetBrowserHitPayloadResolver::FolderDropTargetAt(
    const EditorAssetBrowserHit& hit,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    switch (hit.kind) {
    case EditorAssetBrowserHitKind::FolderDisclosure:
    case EditorAssetBrowserHitKind::Folder:
        return TreeFolderAt(hit.index, state, manager);
    case EditorAssetBrowserHitKind::ContentFolder:
        return ContentFolderAt(hit.index, state, manager);
    case EditorAssetBrowserHitKind::Asset:
    case EditorAssetBrowserHitKind::DropTarget:
        return state.SelectedFolder();
    default:
        return std::nullopt;
    }
}

} // namespace kb::editor

#endif
