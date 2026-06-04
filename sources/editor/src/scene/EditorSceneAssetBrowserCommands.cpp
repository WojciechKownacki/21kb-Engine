#include "scene/EditorSceneAssetBrowserCommands.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <optional>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] bool SameVirtualPath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return kb::assets::NormalizeAssetPath(left) == kb::assets::NormalizeAssetPath(right);
}

[[nodiscard]] kb::assets::AssetManager& AssetManager(kb::scene::Scene& scene) noexcept {
    return scene.Assets().Manager();
}

void RefreshAssets(kb::scene::Scene& scene) {
    static_cast<void>(scene.Assets().Discover());
}

} // namespace

bool EditorSceneAssetBrowserCommands::CommitTextEdit(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const std::string value{ assetBrowser.TextEditValue() };
    if (value.empty()) {
        assetBrowser.CancelTextEdit();
        return false;
    }

    bool committed = false;
    switch (assetBrowser.TextEditMode()) {
    case EditorAssetTextEditMode::NewFolder: {
        const std::filesystem::path parent = assetBrowser.TextEditTargetFolder().empty() ? assetBrowser.SelectedFolder() : assetBrowser.TextEditTargetFolder();
        const std::optional<std::filesystem::path> folder = manager.CreateUniqueFolder(parent, value);
        committed = folder.has_value();
        if (folder.has_value()) {
            RefreshAssets(scene);
            static_cast<void>(assetBrowser.SelectContentFolder(*folder, manager));
        }
        break;
    }
    case EditorAssetTextEditMode::RenameAsset: {
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetBrowser.TextEditTargetAsset());
        if (metadata != nullptr) {
            const std::filesystem::path renamedVirtualPath = metadata->virtualPath.parent_path() / (value + metadata->virtualPath.extension().string());
            committed = manager.RenameAsset(metadata->id, value);
            if (committed) {
                if (const kb::assets::AssetMetadata* renamed = manager.Registry().FindByPath(renamedVirtualPath); renamed != nullptr) {
                    static_cast<void>(assetBrowser.SelectAsset(renamed->id, manager));
                }
            }
        }
        break;
    }
    case EditorAssetTextEditMode::RenameFolder: {
        const std::filesystem::path oldFolder = assetBrowser.TextEditTargetFolder();
        const std::filesystem::path renamedFolder = oldFolder.parent_path() / value;
        const bool renamedOpenFolder = SameVirtualPath(oldFolder, assetBrowser.SelectedFolder());
        committed = manager.RenameFolder(oldFolder, value);
        if (committed) {
            if (renamedOpenFolder) {
                static_cast<void>(assetBrowser.SelectFolder(renamedFolder, manager));
            } else {
                static_cast<void>(assetBrowser.SelectContentFolder(renamedFolder, manager));
            }
        }
        break;
    }
    case EditorAssetTextEditMode::None:
    default:
        break;
    }

    assetBrowser.CancelTextEdit();
    return committed;
}

bool EditorSceneAssetBrowserCommands::DeleteSelected(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser) {
    const kb::assets::AssetId selected = assetBrowser.SelectedAsset();
    const std::filesystem::path selectedContentFolder = assetBrowser.SelectedContentFolder();
    kb::assets::AssetManager& manager = AssetManager(scene);
    const bool deletingAsset = selected.IsValid();
    const bool deletingContentFolder = !selectedContentFolder.empty();
    const std::filesystem::path folderToDelete = deletingContentFolder ? selectedContentFolder : assetBrowser.SelectedFolder();
    const bool deleted = deletingAsset ? manager.DeleteAsset(selected) : manager.DeleteFolder(folderToDelete);
    if (deleted) {
        assetBrowser.ClearSelection();
        if (!deletingAsset && !deletingContentFolder) {
            static_cast<void>(assetBrowser.SelectFolder(assetBrowser.SelectedFolder().parent_path(), manager));
        }
        RefreshAssets(scene);
    }
    return deleted;
}

bool EditorSceneAssetBrowserCommands::DeleteAsset(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, kb::assets::AssetId id) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const bool deleted = manager.DeleteAsset(id);
    if (deleted) {
        assetBrowser.ClearSelection();
        RefreshAssets(scene);
    }
    return deleted;
}

bool EditorSceneAssetBrowserCommands::DeleteFolder(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, const std::filesystem::path& virtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const bool deleted = manager.DeleteFolder(virtualFolder);
    if (deleted) {
        if (SameVirtualPath(assetBrowser.SelectedFolder(), virtualFolder)) {
            static_cast<void>(assetBrowser.SelectFolder(virtualFolder.parent_path(), manager));
        } else if (SameVirtualPath(assetBrowser.SelectedContentFolder(), virtualFolder)) {
            assetBrowser.ClearSelection();
        }
        RefreshAssets(scene);
    }
    return deleted;
}

bool EditorSceneAssetBrowserCommands::MoveAssetToFolder(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    kb::assets::AssetId id,
    const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const kb::assets::AssetMoveResult moved = manager.MoveAssetIntoFolder(id, destinationVirtualFolder);
    if (!moved.succeeded) {
        return false;
    }

    RefreshAssets(scene);
    if (const kb::assets::AssetMetadata* movedMetadata = manager.Registry().FindByPath(moved.virtualPath); movedMetadata != nullptr) {
        static_cast<void>(assetBrowser.SelectAsset(movedMetadata->id, manager));
    }
    return true;
}

bool EditorSceneAssetBrowserCommands::MoveFolderToFolder(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    const std::filesystem::path& sourceVirtualFolder,
    const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const bool movedOpenFolder = SameVirtualPath(assetBrowser.SelectedFolder(), sourceVirtualFolder);
    const kb::assets::AssetMoveResult moved = manager.MoveFolderIntoFolder(sourceVirtualFolder, destinationVirtualFolder);
    if (!moved.succeeded) {
        return false;
    }

    RefreshAssets(scene);
    if (!moved.virtualPath.empty()) {
        if (movedOpenFolder) {
            static_cast<void>(assetBrowser.SelectFolder(moved.virtualPath, manager));
        } else {
            static_cast<void>(assetBrowser.SelectContentFolder(moved.virtualPath, manager));
        }
    }
    return true;
}

} // namespace kb::editor
