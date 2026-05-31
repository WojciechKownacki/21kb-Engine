#include "app/EditorHierarchyEntityAssetDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "project/EditorProjectPanelHitTester.hpp"
#include "project/EditorProjectPaths.hpp"

#include <filesystem>
#include <optional>

namespace kb::editor {

bool EditorHierarchyEntityAssetDropHandler::Drop(HWND sourceWindow, HWND mainWindow, int x, int y, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, EditorSceneContext& sceneContext, kb::scene::SceneEntity entity) {
    const std::optional<RECT> assets = EditorDropPanelResolver::Resolve(DockPanelKind::Assets, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!assets.has_value() || !EditorProjectPanelHitTester::IsPrefabDropTarget(*assets, x, y)) {
        return false;
    }

    const std::filesystem::path path = EditorProjectPaths::UniquePrefabPath(sceneContext.Scene().Entities().Name(entity));
    return sceneContext.CreatePrefabAsset(entity, path);
}

} // namespace kb::editor

#endif
