#include "docking/DockDropPreviewState.hpp"

#if defined(_WIN32)
#include "docking/DockDropPreviewFactory.hpp"
#include "docking/DockWindowLayoutResolver.hpp"

namespace kb::editor {

void DockDropPreviewState::Update(
    POINT screen,
    HWND mainWindow,
    std::uint32_t draggedPanelId,
    EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    std::optional<DockDropPreview>& dropPreview) {
    std::optional<DockDropPreview> nextPreview = ResolveFloatingTarget(screen, mainWindow, draggedPanelId, dockModel, floatingWindows, metrics);
    if (!nextPreview.has_value()) {
        POINT mainPoint{ screen.x, screen.y };
        ScreenToClient(mainWindow, &mainPoint);
        const DockLayout layout = BuildMainLayout(mainWindow, dockModel, metrics);
        nextPreview = dockModel.Queries().ResolveDropPreview(layout, mainPoint.x, mainPoint.y);
    }
    if (!SamePreview(dropPreview, nextPreview)) {
        dropPreview = nextPreview;
        InvalidateRect(mainWindow, nullptr, FALSE);
    }
}

std::optional<DockDropPreview> DockDropPreviewState::ResolveFloatingTarget(
    POINT screen,
    HWND mainWindow,
    std::uint32_t draggedPanelId,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    const DockPanel* dragged = dockModel.Queries().FindPanel(draggedPanelId);
    if (dragged == nullptr || !dragged->detachable) {
        return std::nullopt;
    }
    const EditorFloatingWindowQueries queries = floatingWindows.Queries();
    const HWND draggedWindow = queries.WindowForPanel(draggedPanelId);
    // The topmost floating window under the pointer. The main window ends the search: nothing below it is a target.
    for (HWND window = GetTopWindow(nullptr); window != nullptr && window != mainWindow; window = GetWindow(window, GW_HWNDNEXT)) {
        RECT frame{};
        if (window == draggedWindow || !queries.IsFloatingWindow(window) || IsWindowVisible(window) == 0 ||
            GetWindowRect(window, &frame) == 0 || PtInRect(&frame, screen) == 0) {
            continue;
        }
        const DockLayout layout = DockWindowLayoutResolver::Resolve(window, mainWindow, dockModel, floatingWindows, metrics);
        if (layout.leaves.empty()) {
            return std::nullopt;
        }
        POINT point = screen;
        ScreenToClient(window, &point);
        const DockLeafLayout& leaf = layout.leaves.front();
        DockDropPreview preview = leaf.tabStrip.Contains(point.x, point.y)
            ? DockDropPreviewFactory{}.ForStripMarker(layout, leaf, point.x)
            : DockDropPreview{ .zone = DockDropZone::Center, .kind = DockDropPreviewKind::Glow, .leafId = leaf.leafId, .rect = leaf.content };
        // The preview is drawn over the main window, so it is placed in its client coordinates.
        POINT origin{ 0, 0 };
        ClientToScreen(window, &origin);
        ScreenToClient(mainWindow, &origin);
        preview.rect.x += origin.x;
        preview.rect.y += origin.y;
        return preview;
    }
    return std::nullopt;
}

DockLayout DockDropPreviewState::BuildMainLayout(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
    RECT client{};
    GetClientRect(mainWindow, &client);
    return dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        metrics.menuHeight, metrics.statusBarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize);
}

bool DockDropPreviewState::SamePreview(const std::optional<DockDropPreview>& lhs, const std::optional<DockDropPreview>& rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs.has_value()) {
        return true;
    }

    return lhs->zone == rhs->zone &&
           lhs->kind == rhs->kind &&
           lhs->leafId == rhs->leafId &&
           lhs->tabInsertionIndex == rhs->tabInsertionIndex &&
           SameRect(lhs->rect, rhs->rect);
}

bool DockDropPreviewState::SameRect(const DockRect& lhs, const DockRect& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
}

} // namespace kb::editor

#endif
