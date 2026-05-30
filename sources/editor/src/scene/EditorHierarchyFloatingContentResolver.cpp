#include "scene/EditorHierarchyFloatingContentResolver.hpp"

#if defined(_WIN32)

namespace kb::editor {

std::optional<RECT> EditorHierarchyFloatingContentResolver::Resolve(HWND sourceWindow, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics) {
    const DockPanel* panel = dockModel.Queries().FindPanel(floatingWindows.Queries().PanelId(sourceWindow));
    if (panel == nullptr || panel->kind != DockPanelKind::Hierarchy) {
        return std::nullopt;
    }

    RECT client{};
    GetClientRect(sourceWindow, &client);
    const RECT panelRect{ client.left + 1, client.top + 1, client.right - 1, client.bottom - 1 };
    return RECT{
        panelRect.left + metrics.panelPadding,
        panelRect.top + metrics.panelPadding + metrics.tabStripHeight,
        panelRect.right - metrics.panelPadding,
        panelRect.bottom - metrics.panelPadding,
    };
}

} // namespace kb::editor

#endif
