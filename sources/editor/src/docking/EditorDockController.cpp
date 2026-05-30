#include "docking/EditorDockController.hpp"

#if defined(_WIN32)
#include "docking/DockDragOperationHandler.hpp"
#include "docking/DockHoverCursorUpdater.hpp"
#include "docking/EditorDockPointerDownHandler.hpp"

namespace kb::editor {

void EditorDockController::Configure(HWND mainWindow, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics) noexcept {
    mainWindow_ = mainWindow;
    dockModel_ = &dockModel;
    floatingWindows_ = &floatingWindows;
    metrics_ = &metrics;
}

const DockDropPreview* EditorDockController::DropPreview() const noexcept {
    return dropPreview_ ? &*dropPreview_ : nullptr;
}

void EditorDockController::HandlePointerDown(HWND window, int x, int y) {
    if (!Ready()) {
        return;
    }

    SetFocus(window);
    SetCapture(window);
    EditorDockPointerDownHandler::Handle(window, x, y, mainWindow_, *dockModel_, *floatingWindows_, *metrics_, drag_);
}

void EditorDockController::HandlePointerMove(HWND window, int x, int y) {
    if (!Ready()) {
        return;
    }

    if (!drag_.has_value()) {
        UpdateHoverCursor(window, x, y);
        return;
    }

    DockDragOperationHandler::Move(*drag_, window, x, y, mainWindow_, *dockModel_, *floatingWindows_, *metrics_, dropPreview_);
}

void EditorDockController::HandlePointerUp(HWND window) {
    ReleaseCapture();

    if (!Ready() || !drag_.has_value()) {
        return;
    }

    const DockPointerDrag drag = *drag_;
    drag_.reset();
    DockDragOperationHandler::Complete(drag, window, mainWindow_, *dockModel_, *floatingWindows_, *metrics_, dropPreview_);
}

void EditorDockController::UpdateHoverCursor(HWND window, int x, int y) const {
    if (!Ready()) {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return;
    }

    DockHoverCursorUpdater::Update(window, x, y, mainWindow_, *dockModel_, *metrics_);
}

bool EditorDockController::Ready() const noexcept {
    return mainWindow_ != nullptr && dockModel_ != nullptr && floatingWindows_ != nullptr && metrics_ != nullptr;
}

} // namespace kb::editor

#endif
