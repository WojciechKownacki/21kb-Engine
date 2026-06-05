#include "app/EditorHierarchyEntityHierarchyDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "scene/EditorHierarchyRowPicker.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

bool EditorHierarchyEntityHierarchyDropHandler::Drop(HWND sourceWindow, HWND mainWindow, int x, int y, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, EditorSceneContext& sceneContext, std::span<const kb::scene::SceneEntity> entities) {
    const std::optional<RECT> hierarchy = EditorDropPanelResolver::Resolve(DockPanelKind::Hierarchy, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!hierarchy.has_value() || entities.empty()) {
        return false;
    }

    const kb::scene::SceneEntity parent = EditorHierarchyRowPicker::EntityAtContentPoint(*hierarchy, x, y, sceneContext);
    if (parent.IsValid()) {
        return sceneContext.ReparentEntities(entities, parent);
    }

    const HierarchyToolbarLayoutRects toolbar = HierarchyToolbarLayout::Resolve(*hierarchy);
    return Contains(toolbar.listContent, x, y) && sceneContext.ReparentEntities(entities, {});
}

} // namespace kb::editor

#endif
