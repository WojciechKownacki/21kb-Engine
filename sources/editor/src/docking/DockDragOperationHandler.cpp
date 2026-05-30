#include "docking/DockDragOperationHandler.hpp"

#if defined(_WIN32)
#include "docking/DockDropPreviewState.hpp"
#include "docking/DockFloatingDragOperation.hpp"
#include "docking/DockTabIndexResolver.hpp"

namespace kb::editor {

void DockDragOperationHandler::Move(
    DockPointerDrag& drag,
    HWND eventWindow,
    int x,
    int y,
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    std::optional<DockDropPreview>& dropPreview) {
    if (IsMainWindow(drag.sourceWindow, mainWindow) && drag.kind == DockHitKind::Splitter) {
        MoveSplitter(drag, x, y, mainWindow, dockModel, metrics);
        return;
    }

    if (drag.kind != DockHitKind::Tab || drag.panelId == 0) {
        return;
    }

    POINT screen{ x, y };
    ClientToScreen(eventWindow, &screen);

    if (ReorderDockedTab(drag, x, y, mainWindow, dockModel, metrics)) {
        return;
    }

    DockFloatingDragOperation::EnsureDetached(drag, screen, dockModel, floatingWindows);
    if (!drag.detached) {
        return;
    }

    DockFloatingDragOperation::MoveWindow(drag, screen, dockModel, floatingWindows);
    DockDropPreviewState::Update(screen, mainWindow, dockModel, metrics, dropPreview);
}

void DockDragOperationHandler::Complete(
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

        const DockLayout layout = BuildMainLayout(mainWindow, dockModel, metrics);
        const std::optional<DockDropPreview> preview = dockModel.ResolveDropPreview(layout, mainPoint.x, mainPoint.y);
        if (preview.has_value()) {
            floatingWindows.Destroy(drag.panelId);
            dockModel.DockPanelTo(drag.panelId, *preview);
        } else if (const std::optional<DockRect> rect = floatingWindows.RectForPanel(drag.panelId); rect.has_value()) {
            dockModel.MoveFloatingPanel(drag.panelId, rect->x, rect->y);
            dockModel.ResizeFloatingPanel(drag.panelId, rect->width, rect->height);
        }
    }

    dropPreview.reset();
    InvalidateRect(mainWindow, nullptr, FALSE);
    if (!IsMainWindow(releaseWindow, mainWindow)) {
        InvalidateRect(releaseWindow, nullptr, FALSE);
    }
}

DockLayout DockDragOperationHandler::BuildMainLayout(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
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

bool DockDragOperationHandler::IsMainWindow(HWND candidate, HWND mainWindow) noexcept {
    return candidate == mainWindow;
}

void DockDragOperationHandler::MoveSplitter(DockPointerDrag& drag, int x, int y, HWND mainWindow, EditorDockModel& dockModel, const EditorMetrics& metrics) {
    const DockLayout layout = BuildMainLayout(mainWindow, dockModel, metrics);
    dockModel.ResizeSplitter(drag.splitterNodeId, x, y, layout);
    InvalidateRect(mainWindow, nullptr, FALSE);
}

bool DockDragOperationHandler::ReorderDockedTab(DockPointerDrag& drag, int x, int y, HWND mainWindow, EditorDockModel& dockModel, const EditorMetrics& metrics) {
    if (drag.detached || !drag.sourceStrip.Contains(x, y)) {
        return false;
    }

    const DockLayout layout = BuildMainLayout(mainWindow, dockModel, metrics);
    const std::uint32_t newIndex = DockTabIndexResolver{}.Resolve(layout, drag.sourceLeafId, x);
    if (newIndex != drag.sourceTabIndex) {
        dockModel.ReorderPanelInLeaf(drag.panelId, drag.sourceLeafId, newIndex);
        drag.sourceTabIndex = newIndex;
        InvalidateRect(mainWindow, nullptr, FALSE);
    }
    return true;
}

} // namespace kb::editor

#endif
