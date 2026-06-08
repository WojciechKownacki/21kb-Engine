#include "docking/DockDragOperationHandler.hpp"

#if defined(_WIN32)
#include "docking/DockDockedTabDragHandler.hpp"
#include "docking/DockDragCompletionHandler.hpp"
#include "docking/DockDropPreviewState.hpp"
#include "docking/DockFloatingDragOperation.hpp"
#include "docking/DockSplitterDragHandler.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool TabDragThresholdReached(const DockPointerDrag& drag, int x, int y) noexcept {
    constexpr int kTabDragThresholdPixelsSquared = 36;
    const int dx = x - drag.startX;
    const int dy = y - drag.startY;
    return (dx * dx + dy * dy) >= kTabDragThresholdPixelsSquared;
}

} // namespace

bool DockDragOperationHandler::Move(
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
        DockSplitterDragHandler::Move(drag, x, y, mainWindow, dockModel, metrics);
        return true;
    }

    if (drag.kind != DockHitKind::Tab || drag.panelId == 0) {
        return false;
    }

    if (!TabDragThresholdReached(drag, x, y)) {
        return false;
    }
    drag.manualTabDrag = true;

    POINT screen{ x, y };
    ClientToScreen(eventWindow, &screen);

    if (DockDockedTabDragHandler::Reorder(drag, x, y, mainWindow, dockModel, metrics)) {
        return true;
    }

    DockFloatingDragOperation::EnsureDetached(drag, screen, dockModel, floatingWindows, metrics);
    if (!drag.detached) {
        return false;
    }

    DockFloatingDragOperation::MoveWindow(drag, screen, dockModel, floatingWindows);
    DockDropPreviewState::Update(screen, mainWindow, dockModel, metrics, dropPreview);
    return true;
}

bool DockDragOperationHandler::Complete(
    const DockPointerDrag& drag,
    HWND releaseWindow,
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    std::optional<DockDropPreview>& dropPreview) {
    DockDragCompletionHandler::Complete(drag, releaseWindow, mainWindow, dockModel, floatingWindows, metrics, dropPreview);
    return drag.kind == DockHitKind::Splitter || drag.detached;
}

bool DockDragOperationHandler::IsMainWindow(HWND candidate, HWND mainWindow) noexcept {
    return candidate == mainWindow;
}

} // namespace kb::editor

#endif
