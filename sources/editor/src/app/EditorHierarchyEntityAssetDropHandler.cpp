#include "app/EditorHierarchyEntityAssetDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "project/EditorProjectPaths.hpp"

#include <filesystem>
#include <optional>

namespace kb::editor {

bool EditorHierarchyEntityAssetDropHandler::Drop(HWND sourceWindow, HWND mainWindow, int x, int y, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, EditorSceneContext& sceneContext, kb::scene::SceneEntity entity) {
    const std::optional<RECT> assets = EditorDropPanelResolver::Resolve(DockPanelKind::Assets, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!assets.has_value() || !EditorAssetBrowserHitTester::IsDropTarget(*assets, x, y)) {
        return false;
    }

    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    std::filesystem::path folder = EditorProjectPaths::PrefabsRoot();
    const std::filesystem::path targetFolder = EditorAssetBrowserHitTester::FolderDropTargetAt(
        *assets,
        x,
        y,
        sceneContext.AssetBrowser(),
        manager).value_or(sceneContext.AssetBrowser().SelectedFolder());
    if (kb::assets::NormalizeAssetPath(targetFolder) == "/Game") {
        folder = EditorProjectPaths::AssetsRoot();
    } else if (const std::optional<std::filesystem::path> resolvedFolder = manager.Mounts().Resolve(targetFolder)) {
        folder = *resolvedFolder;
    }
    const std::filesystem::path path = EditorProjectPaths::UniquePrefabPathInFolder(folder, sceneContext.Scene().Entities().Name(entity));
    return sceneContext.CreatePrefabAsset(entity, path);
}

} // namespace kb::editor

#endif
