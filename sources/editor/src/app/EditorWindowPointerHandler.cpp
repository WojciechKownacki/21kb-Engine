#include "app/EditorWindowPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorMouseWheelRouter.hpp"
#include "app/pointer/EditorLeftButtonDoubleClickRouter.hpp"
#include "app/pointer/EditorLeftButtonDownRouter.hpp"
#include "app/pointer/EditorLeftButtonUpRouter.hpp"
#include "app/pointer/EditorMouseMoveRouter.hpp"
#include "app/pointer/EditorRightButtonDownRouter.hpp"
#include "app/pointer/EditorSetCursorRouter.hpp"
#include "app/EditorWindowInvalidator.hpp"

#include <windowsx.h>

namespace kb::editor {

EditorWindowPointerHandler::EditorWindowPointerHandler(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorHierarchySelectionController& hierarchySelection,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorPlayModeState& playMode,
    EditorShellInteractionState& shellInteraction,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , hierarchySelection_(hierarchySelection)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport)
    , playMode_(playMode)
    , shellInteraction_(shellInteraction)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

LRESULT EditorWindowPointerHandler::HandleLeftButtonDown(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    EditorLeftButtonDownRouter leftButtonDown(
        mainWindow_,
        dockModel_,
        floatingWindows_,
        dockController_,
        hierarchySelection_,
        sceneContext_,
        sceneViewport_,
        playMode_,
        shellInteraction_,
        pointerDrag_,
        metrics_);
    leftButtonDown.Handle(messageWindow, x, y);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleRightButtonDown(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    EditorRightButtonDownRouter rightButtonDown(mainWindow_, dockModel_, floatingWindows_, sceneContext_, pointerDrag_, metrics_);
    rightButtonDown.Handle(messageWindow, x, y);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleLeftButtonDoubleClick(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);

    EditorLeftButtonDoubleClickRouter doubleClick(mainWindow_, dockModel_, floatingWindows_, sceneContext_, metrics_);
    if (doubleClick.Handle(messageWindow, x, y)) {
        return 0;
    }

    return HandleLeftButtonDown(messageWindow, lparam);
}

LRESULT EditorWindowPointerHandler::HandleMouseMove(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    const bool leftButtonDown = (wparam & MK_LBUTTON) != 0;
    EditorMouseMoveRouter mouseMove(
        mainWindow_,
        dockModel_,
        floatingWindows_,
        dockController_,
        sceneContext_,
        sceneViewport_,
        shellInteraction_,
        pointerDrag_,
        metrics_);
    mouseMove.Handle(messageWindow, x, y, leftButtonDown);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleMouseWheel(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    ScreenToClient(messageWindow, &point);
    EditorMouseWheelRouter mouseWheel(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_);
    if (mouseWheel.HandleMouseWheel(point.x, point.y, GET_WHEEL_DELTA_WPARAM(wparam))) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return 0;
    }
    return DefWindowProcW(messageWindow, WM_MOUSEWHEEL, wparam, lparam);
}

LRESULT EditorWindowPointerHandler::HandleLeftButtonUp(HWND messageWindow, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);

    EditorLeftButtonUpRouter leftButtonUp(
        mainWindow_,
        dockModel_,
        floatingWindows_,
        dockController_,
        sceneContext_,
        sceneViewport_,
        shellInteraction_,
        pointerDrag_,
        metrics_);
    leftButtonUp.Handle(messageWindow, x, y);
    return 0;
}

LRESULT EditorWindowPointerHandler::HandleSetCursor(HWND messageWindow, WPARAM wparam, LPARAM lparam) {
    EditorSetCursorRouter setCursor(mainWindow_, dockModel_, floatingWindows_, dockController_, sceneContext_, pointerDrag_, metrics_);
    return setCursor.Handle(messageWindow, wparam, lparam);
}

} // namespace kb::editor

#endif
