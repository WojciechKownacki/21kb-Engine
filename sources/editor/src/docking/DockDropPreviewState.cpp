#include "docking/DockDropPreviewState.hpp"

#if defined(_WIN32)

namespace kb::editor {

void DockDropPreviewState::Update(
    POINT screen,
    HWND mainWindow,
    EditorDockModel& dockModel,
    const EditorMetrics& metrics,
    std::optional<DockDropPreview>& dropPreview) {
    POINT mainPoint{ screen.x, screen.y };
    ScreenToClient(mainWindow, &mainPoint);
    const DockLayout layout = BuildMainLayout(mainWindow, dockModel, metrics);
    const std::optional<DockDropPreview> nextPreview = dockModel.ResolveDropPreview(layout, mainPoint.x, mainPoint.y);
    if (!SamePreview(dropPreview, nextPreview)) {
        dropPreview = nextPreview;
        InvalidateRect(mainWindow, nullptr, FALSE);
    }
}

DockLayout DockDropPreviewState::BuildMainLayout(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
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

bool DockDropPreviewState::SamePreview(const std::optional<DockDropPreview>& lhs, const std::optional<DockDropPreview>& rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs.has_value()) {
        return true;
    }

    return lhs->zone == rhs->zone && lhs->kind == rhs->kind && lhs->leafId == rhs->leafId && SameRect(lhs->rect, rhs->rect);
}

bool DockDropPreviewState::SameRect(const DockRect& lhs, const DockRect& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
}

} // namespace kb::editor

#endif
