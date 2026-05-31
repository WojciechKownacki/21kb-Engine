#include "rendering/EditorPanelContentResolver.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] DockLayout BuildLayout(HWND window, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
    RECT client{};
    GetClientRect(window, &client);
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

} // namespace

std::optional<RECT> EditorPanelContentResolver::Resolve(
    DockPanelKind kind,
    HWND sourceWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    if (sourceWindow == nullptr || mainWindow == nullptr) {
        return std::nullopt;
    }

    if (sourceWindow != mainWindow) {
        const DockPanel* panel = dockModel.Queries().FindPanel(floatingWindows.Queries().PanelId(sourceWindow));
        if (panel == nullptr || panel->kind != kind) {
            return std::nullopt;
        }
        RECT client{};
        GetClientRect(sourceWindow, &client);
        client.top += metrics.floatingChromeHeight;
        return client;
    }

    const DockLayout layout = BuildLayout(mainWindow, dockModel, metrics);
    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel != nullptr && panelLayout.active && panel->kind == kind) {
            return GdiDrawing::ToRect(panelLayout.content);
        }
    }
    return std::nullopt;
}

} // namespace kb::editor

#endif
