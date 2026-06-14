#include "app/EditorAssetProjectFilesDropActionExecutor.hpp"

#if defined(_WIN32)
#include "app/EditorAssetDropActionMenu.hpp"
#include "engine/scene/SceneAssets.hpp"

namespace kb::editor {
namespace {

void RestoreCurrentFolder(EditorSceneContext& sceneContext, const std::filesystem::path& currentFolder) {
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    static_cast<void>(sceneContext.AssetBrowser().SelectFolder(currentFolder, manager));
}

} // namespace

bool EditorAssetProjectFilesDropActionExecutor::ExecuteAssetDropMenu(
    HWND sourceWindow,
    int x,
    int y,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    const std::filesystem::path& targetFolder) {
    const std::filesystem::path currentFolder = sceneContext.AssetBrowser().SelectedFolder();
    const EditorAssetDropAction action = EditorAssetDropActionMenu::Show(sourceWindow, x, y);
    bool executed = false;
    if (action == EditorAssetDropAction::MoveHere) {
        executed = sceneContext.MoveAssetToFolder(assetId, targetFolder);
    } else if (action == EditorAssetDropAction::CopyHere) {
        executed = sceneContext.CopyAssetToFolder(assetId, targetFolder);
    }
    if (executed) {
        RestoreCurrentFolder(sceneContext, currentFolder);
    }
    return executed;
}

bool EditorAssetProjectFilesDropActionExecutor::ExecuteFolderDropMenu(
    HWND sourceWindow,
    int x,
    int y,
    EditorSceneContext& sceneContext,
    const std::filesystem::path& sourceVirtualFolder,
    const std::filesystem::path& targetFolder) {
    const std::filesystem::path currentFolder = sceneContext.AssetBrowser().SelectedFolder();
    const EditorAssetDropAction action = EditorAssetDropActionMenu::Show(sourceWindow, x, y);
    bool executed = false;
    if (action == EditorAssetDropAction::MoveHere) {
        executed = sceneContext.MoveAssetFolderToFolder(sourceVirtualFolder, targetFolder);
    } else if (action == EditorAssetDropAction::CopyHere) {
        executed = sceneContext.CopyAssetFolderToFolder(sourceVirtualFolder, targetFolder);
    }
    if (executed) {
        RestoreCurrentFolder(sceneContext, currentFolder);
    }
    return executed;
}

} // namespace kb::editor

#endif
