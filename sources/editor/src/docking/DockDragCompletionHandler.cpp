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
        POINT screen{};
        GetCursorPos(&screen);
        POINT mainPoint{ screen.x, screen.y };
        ScreenToClient(mainWindow, &mainPoint);

        const DockLayout layout = DockMainLayoutResolver::Resolve(mainWindow, dockModel, metrics);
        const std::optional<DockDropPreview> preview = dockModel.Queries().ResolveDropPreview(layout, mainPoint.x, mainPoint.y);
        if (preview.has_value()) {
            floatingWindows.Commands().Destroy(drag.panelId);
            dockModel.Commands().DockPanelTo(drag.panelId, *preview);
        } else if (const std::optional<DockRect> rect = floatingWindows.Queries().RectForPanel(drag.panelId); rect.has_value()) {
            dockModel.Commands().MoveFloatingPanel(drag.panelId, rect->x, rect->y);
            dockModel.Commands().ResizeFloatingPanel(drag.panelId, rect->width, rect->height);
        }
    }

    dropPreview.reset();
    InvalidateRect(mainWindow, nullptr, FALSE);
    if (!IsMainWindow(releaseWindow, mainWindow)) {
        InvalidateRect(releaseWindow, nullptr, FALSE);
    }
}

bool DockDragCompletionHandler::IsMainWindow(HWND candidate, HWND mainWindow) noexcept {
    return candidate == mainWindow;
}

} // namespace kb::editor

#endif
