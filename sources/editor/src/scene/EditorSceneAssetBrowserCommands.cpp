#include "scene/EditorSceneAssetBrowserCommands.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] bool SameVirtualPath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return kb::assets::NormalizeAssetPath(left) == kb::assets::NormalizeAssetPath(right);
}

[[nodiscard]] bool SameOrDescendantVirtualPath(const std::filesystem::path& parent, const std::filesystem::path& candidate) {
    const std::string parentText = kb::assets::NormalizeAssetPath(parent);
    const std::string candidateText = kb::assets::NormalizeAssetPath(candidate);
    return candidateText == parentText || candidateText.starts_with(parentText + "/");
}

[[nodiscard]] kb::assets::AssetManager& AssetManager(kb::scene::Scene& scene) noexcept {
    return scene.Assets().Manager();
}

void RefreshAssets(kb::scene::Scene& scene) {
    static_cast<void>(scene.Assets().Discover());
}

[[nodiscard]] std::filesystem::path UniquePathInFolder(const std::filesystem::path& folder, const std::filesystem::path& filename) {
    std::filesystem::path candidate = folder / filename;
    std::error_code error;
    if (!std::filesystem::exists(candidate, error)) {
        return candidate;
    }

    const std::string stem = filename.stem().string();
    const std::string extension = filename.extension().string();
    for (int suffix = 2; suffix < 10000; ++suffix) {
        candidate = folder / (stem + "_" + std::to_string(suffix) + extension);
        error.clear();
        if (!std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }
    return {};
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
    kb::assets::AssetManager& manager = AssetManager(scene);

    std::vector<kb::assets::AssetId> assetsToDelete;
    std::vector<std::filesystem::path> foldersToDelete;
    for (const EditorAssetSelectionSummaryRow& row : assetBrowser.CheckedDeleteTargetRows(manager)) {
        if (row.key.rfind("Asset:", 0) == 0) {
            kb::assets::AssetId id{};
            if (kb::assets::TryParseAssetId(std::string_view(row.key).substr(6), id) && id.IsValid()) {
                assetsToDelete.push_back(id);
            }
        } else if (row.key.rfind("Folder:", 0) == 0) {
            foldersToDelete.push_back(row.id);
        }
    }

    if (assetsToDelete.empty() && foldersToDelete.empty()) {
        return false;
    }

    const std::filesystem::path openFolderBeforeDelete = assetBrowser.SelectedFolder();
    bool deleted = false;
    for (const kb::assets::AssetId asset : assetsToDelete) {
        deleted = manager.DeleteAsset(asset) || deleted;
    }
    for (const std::filesystem::path& folder : foldersToDelete) {
        deleted = manager.DeleteFolder(folder) || deleted;
    }

    if (deleted) {
        assetBrowser.ClearSelection();
        for (const std::filesystem::path& folder : foldersToDelete) {
            if (SameOrDescendantVirtualPath(folder, openFolderBeforeDelete)) {
                static_cast<void>(assetBrowser.SelectFolder(folder.parent_path(), manager));
                break;
            }
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

bool EditorSceneAssetBrowserCommands::CopyAssetToFolder(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    kb::assets::AssetId id,
    const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr) {
        return false;
    }

    const std::filesystem::path source = metadata->physicalPath.empty()
        ? manager.Mounts().Resolve(metadata->virtualPath).value_or(std::filesystem::path{})
        : metadata->physicalPath;
    const std::optional<std::filesystem::path> destinationFolder = manager.Mounts().Resolve(destinationVirtualFolder);
    if (source.empty() || !destinationFolder.has_value()) {
        return false;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || !std::filesystem::is_directory(*destinationFolder, error)) {
        return false;
    }

    const std::filesystem::path destination = UniquePathInFolder(*destinationFolder, source.filename());
    if (destination.empty()) {
        return false;
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
    if (error) {
        return false;
    }

    RefreshAssets(scene);
    if (const std::optional<std::filesystem::path> copiedVirtualPath = manager.Mounts().ToVirtual(destination); copiedVirtualPath.has_value()) {
        if (const kb::assets::AssetMetadata* copied = manager.Registry().FindByPath(*copiedVirtualPath); copied != nullptr) {
            static_cast<void>(assetBrowser.SelectAsset(copied->id, manager));
        }
    }
    return true;
}

bool EditorSceneAssetBrowserCommands::CopyFolderToFolder(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    const std::filesystem::path& sourceVirtualFolder,
    const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    if (SameOrDescendantVirtualPath(sourceVirtualFolder, destinationVirtualFolder)) {
        return false;
    }

    const std::optional<std::filesystem::path> source = manager.Mounts().Resolve(sourceVirtualFolder);
    const std::optional<std::filesystem::path> destinationFolder = manager.Mounts().Resolve(destinationVirtualFolder);
    if (!source.has_value() || !destinationFolder.has_value()) {
        return false;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(*source, error) || !std::filesystem::is_directory(*destinationFolder, error)) {
        return false;
    }

    std::filesystem::path destination = UniquePathInFolder(*destinationFolder, source->filename());
    if (destination.empty()) {
        return false;
    }
    std::filesystem::copy(*source, destination, std::filesystem::copy_options::recursive, error);
    if (error) {
        return false;
    }

    RefreshAssets(scene);
    if (const std::optional<std::filesystem::path> copiedVirtualPath = manager.Mounts().ToVirtual(destination); copiedVirtualPath.has_value()) {
        static_cast<void>(assetBrowser.SelectContentFolder(*copiedVirtualPath, manager));
    }
    return true;
}

} // namespace kb::editor
