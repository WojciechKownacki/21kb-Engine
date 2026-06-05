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

bool EditorDockController::HandlePointerDown(HWND window, int x, int y) {
    if (!Ready()) {
        return false;
    }

    CancelDrag();
    SetFocus(window);
    const bool handled = EditorDockPointerDownHandler::Handle(window, x, y, mainWindow_, *dockModel_, *floatingWindows_, *metrics_, drag_);
    if (drag_.has_value()) {
        CaptureSourceWindow();
    }
    return handled;
}

bool EditorDockController::HandlePointerMove(HWND window, int x, int y, bool leftButtonDown) {
    if (!Ready()) {
        return false;
    }

    if (!drag_.has_value()) {
        UpdateHoverCursor(window, x, y);
        return false;
    }

    if (!leftButtonDown) {
        CancelDrag();
        UpdateHoverCursor(window, x, y);
        return false;
    }

    CaptureSourceWindow();
    return DockDragOperationHandler::Move(*drag_, window, x, y, mainWindow_, *dockModel_, *floatingWindows_, *metrics_, dropPreview_);
}

bool EditorDockController::HandlePointerUp(HWND window) {
    if (!Ready() || !drag_.has_value()) {
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        return false;
    }

    const DockPointerDrag drag = *drag_;
    const HWND capturedWindow = drag.sourceWindow;
    drag_.reset();
    if (GetCapture() == window || GetCapture() == capturedWindow) {
        ReleaseCapture();
    }
    return DockDragOperationHandler::Complete(drag, window, mainWindow_, *dockModel_, *floatingWindows_, *metrics_, dropPreview_);
}

void EditorDockController::CancelDrag() noexcept {
    const HWND capturedWindow = drag_.has_value() ? drag_->sourceWindow : nullptr;
    drag_.reset();
    dropPreview_.reset();
    if (capturedWindow != nullptr && GetCapture() == capturedWindow) {
        ReleaseCapture();
    }
    if (mainWindow_ != nullptr) {
        InvalidateRect(mainWindow_, nullptr, FALSE);
    }
}

void EditorDockController::HandleCaptureChanged(HWND newCapture) noexcept {
    if (!drag_.has_value()) {
        return;
    }
    if (newCapture == drag_->sourceWindow) {
        return;
    }
    if (LeftButtonPressed()) {
        CaptureSourceWindow();
        return;
    }

    drag_.reset();
    dropPreview_.reset();
    if (mainWindow_ != nullptr) {
        InvalidateRect(mainWindow_, nullptr, FALSE);
    }
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

bool EditorDockController::LeftButtonPressed() const noexcept {
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

void EditorDockController::CaptureSourceWindow() const noexcept {
    if (!drag_.has_value() || drag_->sourceWindow == nullptr || IsWindow(drag_->sourceWindow) == 0) {
        return;
    }
    if (GetCapture() != drag_->sourceWindow) {
        SetCapture(drag_->sourceWindow);
    }
}

} // namespace kb::editor

#endif
