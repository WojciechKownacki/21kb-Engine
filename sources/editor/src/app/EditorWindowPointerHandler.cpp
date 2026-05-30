#include "app/EditorWindowPointerHandler.hpp"

#if defined(_WIN32)
#include <windowsx.h>

namespace kb::editor {

EditorWindowPointerHandler::EditorWindowPointerHandler(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorHierarchySelectionController& hierarchySelection,
    EditorSceneContext& sceneContext,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , hierarchySelection_(hierarchySelection)
    , sceneContext_(sceneContext)
    , metrics_(metrics) {}

LRESULT EditorWindowPointerHandler::HandleLeftButtonDown(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);

    if (hierarchySelection_.HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        InvalidateRect(mainWindow_, nullptr, FALSE);
        if (!IsMainWindow(messageWindow)) {
            InvalidateRect(messageWindow, nullptr, FALSE);
        }
        return 0;
    }

    dockController_.HandlePointerDown(messageWindow, x, y);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleMouseMove(HWND messageWindow, LPARAM lparam) {
    dockController_.HandlePointerMove(messageWindow, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleLeftButtonUp(HWND messageWindow) {
    dockController_.HandlePointerUp(messageWindow);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleSetCursor(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    if (LOWORD(lparam) != HTCLIENT) {
        return DefWindowProcW(messageWindow, WM_SETCURSOR, wparam, lparam);
    }

    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(messageWindow, &point);
    dockController_.UpdateHoverCursor(messageWindow, point.x, point.y);
    return TRUE;
}

bool EditorWindowPointerHandler::IsMainWindow(HWND candidate) const noexcept {
    return candidate == mainWindow_;
}

} // namespace kb::editor

#endif
