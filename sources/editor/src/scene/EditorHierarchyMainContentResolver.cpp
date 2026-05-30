#include "scene/EditorHierarchyMainContentResolver.hpp"

#if defined(_WIN32)

namespace kb::editor {

std::optional<RECT> EditorHierarchyMainContentResolver::Resolve(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
    const DockLayout layout = BuildLayout(mainWindow, dockModel, metrics);
    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || !panelLayout.active || panel->kind != DockPanelKind::Hierarchy) {
            continue;
        }

        return RECT{
            panelLayout.content.x,
            panelLayout.content.y,
            panelLayout.content.x + panelLayout.content.width,
            panelLayout.content.y + panelLayout.content.height,
        };
    }

    return std::nullopt;
}

DockLayout EditorHierarchyMainContentResolver::BuildLayout(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
    RECT client{};
    GetClientRect(mainWindow, &client);
    return dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);
}

} // namespace kb::editor

#endif
