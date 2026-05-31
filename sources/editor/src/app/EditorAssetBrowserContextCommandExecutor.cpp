#include "app/EditorAssetBrowserContextCommandExecutor.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/EditorSceneContext.hpp"

#include <filesystem>

namespace kb::editor {

bool EditorAssetBrowserContextCommandExecutor::Execute(EditorAssetContextCommand command, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetContextTargetKind targetKind = state.ContextMenuTargetKind();
    const kb::assets::AssetId targetAsset = state.ContextMenuTargetAsset();
    const std::filesystem::path targetFolder = state.ContextMenuTargetFolder();

    switch (command) {
    case EditorAssetContextCommand::NewFolder:
        if (targetKind == EditorAssetContextTargetKind::Folder) {
            static_cast<void>(state.SelectFolder(targetFolder, manager));
        }
        return sceneContext.BeginAssetFolderCreation();
    case EditorAssetContextCommand::Rename:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.BeginAssetRename(targetAsset);
        }
        if (targetKind == EditorAssetContextTargetKind::Folder) {
            return sceneContext.BeginAssetFolderRename(targetFolder);
        }
        return sceneContext.BeginAssetRename();
    case EditorAssetContextCommand::Delete:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.DeleteAssetBrowserItem(targetAsset);
        }
        if (targetKind == EditorAssetContextTargetKind::Folder) {
            return sceneContext.DeleteAssetBrowserFolder(targetFolder);
        }
        return sceneContext.DeleteSelectedAssetBrowserItem();
    case EditorAssetContextCommand::Refresh:
        static_cast<void>(sceneContext.Scene().Assets().Discover());
        return true;
    case EditorAssetContextCommand::None:
    default:
        return false;
    }
}

} // namespace kb::editor
