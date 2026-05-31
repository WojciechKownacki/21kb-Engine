#include "app/EditorPrefabAssetHierarchyDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"

#include <optional>

namespace kb::editor {

bool EditorPrefabAssetHierarchyDropHandler::Drop(HWND sourceWindow, HWND mainWindow, int x, int y, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const std::filesystem::path& assetPath) {
    const std::optional<RECT> hierarchy = EditorDropPanelResolver::Resolve(DockPanelKind::Hierarchy, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!hierarchy.has_value()) {
        return false;
    }

    const kb::scene::SceneEntity parent = EditorHierarchyRowPicker::EntityAtContentPoint(*hierarchy, x, y, sceneContext);
    return sceneContext.InstantiatePrefabAsset(assetPath, parent);
}

} // namespace kb::editor

#endif
