#include "scene/EditorHierarchySelectionController.hpp"

#if defined(_WIN32)
#include "scene/EditorHierarchyMetrics.hpp"

namespace kb::editor {

bool EditorHierarchySelectionController::HandlePointerDown(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) const {
    if (sourceWindow == nullptr || mainWindow == nullptr) {
        return false;
    }

    if (sourceWindow == mainWindow) {
        const DockLayout layout = BuildMainLayout(mainWindow, dockModel, metrics);
        for (const DockPanelLayout& panelLayout : layout.panels) {
            const DockPanel* panel = dockModel.FindPanel(panelLayout.panelId);
            if (panel == nullptr || !panelLayout.active || panel->kind != DockPanelKind::Hierarchy) {
                continue;
            }

            const RECT content{
                panelLayout.content.x,
                panelLayout.content.y,
                panelLayout.content.x + panelLayout.content.width,
                panelLayout.content.y + panelLayout.content.height,
            };
            return SelectAtContentPoint(content, x, y, sceneContext);
        }
        return false;
    }

    const DockPanel* panel = dockModel.FindPanel(floatingWindows.PanelId(sourceWindow));
    if (panel == nullptr || panel->kind != DockPanelKind::Hierarchy) {
        return false;
    }

    RECT client{};
    GetClientRect(sourceWindow, &client);
    const RECT panelRect{ client.left + 1, client.top + 1, client.right - 1, client.bottom - 1 };
    const RECT content{
        panelRect.left + metrics.panelPadding,
        panelRect.top + metrics.panelPadding + metrics.tabStripHeight,
        panelRect.right - metrics.panelPadding,
        panelRect.bottom - metrics.panelPadding,
    };

    return SelectAtContentPoint(content, x, y, sceneContext);
}

bool EditorHierarchySelectionController::SelectAtContentPoint(const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    if (x < content.left || x >= content.right || y < content.top || y >= content.bottom) {
        return false;
    }

    const int relativeY = y - content.top;
    const std::size_t rowIndex = static_cast<std::size_t>(relativeY / kHierarchyRowHeight);
    [[maybe_unused]] const bool selected = sceneContext.SelectHierarchyRow(rowIndex);
    return true;
}

DockLayout EditorHierarchySelectionController::BuildMainLayout(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
    RECT client{};
    GetClientRect(mainWindow, &client);
    return dockModel.BuildLayout(
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
