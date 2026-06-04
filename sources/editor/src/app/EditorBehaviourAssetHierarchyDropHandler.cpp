#include "app/EditorBehaviourAssetHierarchyDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"

#include <optional>

namespace kb::editor {

bool EditorBehaviourAssetHierarchyDropHandler::Drop(HWND sourceWindow, HWND mainWindow, int x, int y, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, EditorSceneContext& sceneContext, kb::assets::AssetId assetId) {
    const std::optional<RECT> hierarchy = EditorDropPanelResolver::Resolve(DockPanelKind::Hierarchy, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!hierarchy.has_value()) {
        return false;
    }

    const kb::scene::SceneEntity target = EditorHierarchyRowPicker::EntityAtContentPoint(*hierarchy, x, y, sceneContext);
    return target.IsValid() && sceneContext.AddBehaviourAssetToEntity(assetId, target);
}

} // namespace kb::editor

#endif
