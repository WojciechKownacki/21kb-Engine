#include "app/EditorAssetBrowserPointerPanelResolver.hpp"

#if defined(_WIN32)
#include "rendering/EditorPanelContentResolver.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{ rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
}

} // namespace

std::optional<RECT> EditorAssetBrowserPointerPanelResolver::ResolveContent(
    HWND sourceWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    return EditorPanelContentResolver::Resolve(DockPanelKind::Assets, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
}

RECT EditorAssetBrowserPointerPanelResolver::ResolveDeleteConfirmOverlayBounds(
    HWND sourceWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorMetrics& metrics,
    const RECT& fallback) noexcept {
    if (sourceWindow == nullptr || mainWindow == nullptr) {
        return fallback;
    }

    if (sourceWindow != mainWindow) {
        RECT client{};
        GetClientRect(sourceWindow, &client);
        return client;
    }

    RECT client{};
    GetClientRect(mainWindow, &client);
    const DockLayout layout = dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);
    return ToRect(layout.workspace);
}

} // namespace kb::editor

#endif
