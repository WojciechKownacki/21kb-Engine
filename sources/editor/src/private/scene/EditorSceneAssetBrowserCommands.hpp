#pragma once

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetImportTypes.hpp"

#include <filesystem>
#include <span>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

// The scene open in the editor and the project's startup scene (physical paths). Project Files does
// not delete, rename or move either, nor a folder holding one - like Unreal refuses the open level.
struct EditorProtectedScenes {
    std::filesystem::path openScene;
    std::filesystem::path startupScene;
};

class EditorSceneAssetBrowserCommands {
public:
    EditorSceneAssetBrowserCommands() = delete;

    [[nodiscard]] static bool CommitTextEdit(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser,
        const EditorProtectedScenes& protectedScenes = {});
    [[nodiscard]] static bool DeleteSelected(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser,
        const EditorProtectedScenes& protectedScenes = {});
    [[nodiscard]] static bool DeleteAsset(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, kb::assets::AssetId id,
        const EditorProtectedScenes& protectedScenes = {});
    [[nodiscard]] static bool DeleteFolder(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, const std::filesystem::path& virtualFolder,
        const EditorProtectedScenes& protectedScenes = {});
    [[nodiscard]] static bool MoveAssetToFolder(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& assetBrowser,
        kb::assets::AssetId id,
        const std::filesystem::path& destinationVirtualFolder,
        const EditorProtectedScenes& protectedScenes = {});
    [[nodiscard]] static bool MoveFolderToFolder(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& assetBrowser,
        const std::filesystem::path& sourceVirtualFolder,
        const std::filesystem::path& destinationVirtualFolder,
        const EditorProtectedScenes& protectedScenes = {});
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
        const std::filesystem::path& destinationVirtualFolder,
        const kb::assets::AssetImportOptions& options = {});
    [[nodiscard]] static kb::assets::AssetImportResult ImportFilesWithReport(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& assetBrowser,
        std::span<const std::filesystem::path> sourceFiles,
        const std::filesystem::path& destinationVirtualFolder,
        const kb::assets::AssetImportOptions& options = {});
};

} // namespace kb::editor
