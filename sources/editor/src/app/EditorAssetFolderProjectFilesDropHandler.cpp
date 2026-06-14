#include "app/EditorAssetFolderProjectFilesDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorAssetProjectFilesDropActionExecutor.hpp"
#include "app/EditorDropPanelResolver.hpp"
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <optional>

namespace kb::editor {

bool EditorAssetFolderProjectFilesDropHandler::Drop(HWND sourceWindow, HWND mainWindow, int x, int y, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const std::filesystem::path& sourceVirtualFolder) {
    const std::optional<RECT> assets = EditorDropPanelResolver::Resolve(DockPanelKind::Assets, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!assets.has_value() || !EditorAssetBrowserHitTester::IsDropTarget(*assets, x, y)) {
        return false;
    }

    if (!sourceVirtualFolder.empty()) {
        const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
        const std::optional<std::filesystem::path> targetFolder = EditorAssetBrowserHitTester::FolderDropTargetAt(*assets, x, y, sceneContext.AssetBrowser(), manager);
        if (targetFolder.has_value()) {
            static_cast<void>(EditorAssetProjectFilesDropActionExecutor::ExecuteFolderDropMenu(
                sourceWindow,
                x,
                y,
                sceneContext,
                sourceVirtualFolder,
                *targetFolder));
        } else {
            static_cast<void>(sceneContext.MoveAssetFolderToFolder(sourceVirtualFolder, sceneContext.AssetBrowser().SelectedFolder()));
        }
    }
    return true;
}

} // namespace kb::editor

#endif
