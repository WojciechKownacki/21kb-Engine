#include "docking/DockDragCompletionHandler.hpp"

#if defined(_WIN32)

#include "docking/DockMainLayoutResolver.hpp"

namespace kb::editor {

void DockDragCompletionHandler::Complete(
    const DockPointerDrag& drag,
    HWND releaseWindow,
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    std::optional<DockDropPreview>& dropPreview) {
    if (drag.detached && drag.panelId != 0) {
        const std::optional<DockDropPreview> target =
            dropPreview.has_value() ? dropPreview : ResolvePreviewAtCursor(mainWindow, dockModel, metrics);
        if (target.has_value()) {
            dockModel.Commands().DockPanelTo(drag.panelId, *target);
            floatingWindows.Commands().Destroy(drag.panelId);
        } else if (const std::optional<DockRect> rect = floatingWindows.Queries().RectForPanel(drag.panelId); rect.has_value()) {
            dockModel.Commands().MoveFloatingPanel(drag.panelId, rect->x, rect->y);
            dockModel.Commands().ResizeFloatingPanel(drag.panelId, rect->width, rect->height);
        }
    }

    dropPreview.reset();
    InvalidateRect(mainWindow, nullptr, FALSE);
    if (!IsMainWindow(releaseWindow, mainWindow) && releaseWindow != nullptr && IsWindow(releaseWindow) != 0) {
        InvalidateRect(releaseWindow, nullptr, FALSE);
    }
}

std::optional<DockDropPreview> DockDragCompletionHandler::ResolvePreviewAtCursor(
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorMetrics& metrics) {
    POINT screen{};
    GetCursorPos(&screen);
    POINT mainPoint{ screen.x, screen.y };
    ScreenToClient(mainWindow, &mainPoint);

    const DockLayout layout = DockMainLayoutResolver::Resolve(mainWindow, dockModel, metrics);
    return dockModel.Queries().ResolveDropPreview(layout, mainPoint.x, mainPoint.y);
}

bool DockDragCompletionHandler::IsMainWindow(HWND candidate, HWND mainWindow) noexcept {
    return candidate == mainWindow;
}

} // namespace kb::editor

#endif
