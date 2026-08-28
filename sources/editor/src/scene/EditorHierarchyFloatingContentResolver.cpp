#include "scene/EditorHierarchyFloatingContentResolver.hpp"

#if defined(_WIN32)
#include "rendering/FloatingPanelGeometry.hpp"

namespace kb::editor {

std::optional<RECT> EditorHierarchyFloatingContentResolver::Resolve(HWND sourceWindow, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics) {
    const DockPanel* panel = dockModel.Queries().FindPanel(floatingWindows.Queries().PanelId(sourceWindow));
    if (panel == nullptr || panel->kind != DockPanelKind::Hierarchy) {
        return std::nullopt;
    }

    RECT client{};
    GetClientRect(sourceWindow, &client);
    return FloatingPanelGeometry::Content(client, metrics.tabStripHeight);
}

} // namespace kb::editor

#endif
