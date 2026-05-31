#include "app/EditorHierarchyEntityHierarchyDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"

#include <optional>

namespace kb::editor {

bool EditorHierarchyEntityHierarchyDropHandler::Drop(HWND sourceWindow, HWND mainWindow, int x, int y, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, EditorSceneContext& sceneContext, kb::scene::SceneEntity entity) {
    const std::optional<RECT> hierarchy = EditorDropPanelResolver::Resolve(DockPanelKind::Hierarchy, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!hierarchy.has_value()) {
        return false;
    }

    const kb::scene::SceneEntity parent = EditorHierarchyRowPicker::EntityAtContentPoint(*hierarchy, x, y, sceneContext);
    return parent.IsValid() && sceneContext.ReparentEntity(entity, parent);
}

} // namespace kb::editor

#endif
