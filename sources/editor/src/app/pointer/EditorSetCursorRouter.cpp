#include "app/pointer/EditorSetCursorRouter.hpp"

#if defined(_WIN32)
#include "app/EditorPointerDragInteraction.hpp"
#include "app/cursor/EditorInternalSplitterCursorController.hpp"

namespace kb::editor {

EditorSetCursorRouter::EditorSetCursorRouter(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , sceneContext_(sceneContext)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

LRESULT EditorSetCursorRouter::Handle(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    if (LOWORD(lparam) != HTCLIENT) {
        return DefWindowProcW(messageWindow, WM_SETCURSOR, wparam, lparam);
    }

    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(messageWindow, &point);
    if (EditorPointerDragInteraction::UpdateCursor(pointerDrag_)) {
        return TRUE;
    }
    const EditorInternalSplitterCursorController splitterCursor(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_);
    if (splitterCursor.HitsResizableSplitter(point.x, point.y)) {
        splitterCursor.UpdateCursor(point.x, point.y);
        return TRUE;
    }
    dockController_.UpdateHoverCursor(messageWindow, point.x, point.y);
    return TRUE;
}

} // namespace kb::editor

#endif
