#pragma once

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"

#include <filesystem>
#include <span>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorSceneAssetBrowserCommands {
public:
    EditorSceneAssetBrowserCommands() = delete;

    [[nodiscard]] static bool CommitTextEdit(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser);
    [[nodiscard]] static bool DeleteSelected(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser);
    [[nodiscard]] static bool DeleteAsset(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, kb::assets::AssetId id);
    [[nodiscard]] static bool DeleteFolder(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, const std::filesystem::path& virtualFolder);
    [[nodiscard]] static bool MoveAssetToFolder(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& assetBrowser,
        kb::assets::AssetId id,
        const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] static bool MoveFolderToFolder(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& assetBrowser,
        const std::filesystem::path& sourceVirtualFolder,
        const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] static bool CopyAssetToFolder(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& assetBrowser,
        kb::assets::AssetId id,
        const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] static bool CopyFolderToFolder(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& assetBrowser,
        const std::filesystem::path& sourceVirtualFolder,
        const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] static bool ImportFiles(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& assetBrowser,
        std::span<const std::filesystem::path> sourceFiles,
        const std::filesystem::path& destinationVirtualFolder);
};

} // namespace kb::editor
