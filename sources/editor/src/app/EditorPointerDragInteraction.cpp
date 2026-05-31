#include "app/EditorPointerDragInteraction.hpp"

#if defined(_WIN32)
#include "app/EditorPointerDropHandler.hpp"
#include "app/EditorWindowInvalidator.hpp"

namespace kb::editor {
namespace {

void ApplyDragCursor() noexcept {
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
}

} // namespace

void EditorPointerDragInteraction::CaptureIfActive(HWND messageWindow, const EditorPointerDragState& drag) noexcept {
    if (drag.Active()) {
        SetCapture(messageWindow);
    }
}

bool EditorPointerDragInteraction::Move(HWND sourceWindow, HWND mainWindow, int x, int y, EditorPointerDragState& drag) noexcept {
    if (!drag.Active()) {
        return false;
    }

    drag.x = x;
    drag.y = y;
    ApplyDragCursor();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow, sourceWindow);
    return true;
}

bool EditorPointerDragInteraction::Complete(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    drag.x = x;
    drag.y = y;

    bool handledDrop = false;
    if (drag.Active()) {
        handledDrop = EditorPointerDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag);
    }

    drag.Clear();
    ReleaseCapture();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow, sourceWindow);
    return handledDrop;
}

bool EditorPointerDragInteraction::UpdateCursor(const EditorPointerDragState& drag) noexcept {
    if (!drag.Active()) {
        return false;
    }

    ApplyDragCursor();
    return true;
}

} // namespace kb::editor

#endif
