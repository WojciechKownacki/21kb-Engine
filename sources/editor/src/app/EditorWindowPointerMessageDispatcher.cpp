#include "app/EditorWindowPointerMessageDispatcher.hpp"

#if defined(_WIN32)
#include "app/EditorWindowPointerHandler.hpp"

namespace kb::editor {

EditorWindowPointerMessageDispatcher::EditorWindowPointerMessageDispatcher(EditorWindowMessageContext context) noexcept
    : context_(context) {}

LRESULT EditorWindowPointerMessageDispatcher::Dispatch(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) const {
    EditorWindowPointerHandler pointerHandler{
        context_.mainWindow,
        context_.dockModel,
        context_.floatingWindows,
        context_.dockController,
        context_.hierarchySelection,
        context_.sceneContext,
        context_.renderBackendSettings,
        context_.sceneViewport,
        context_.playMode,
        context_.shellInteraction,
        context_.pointerDrag,
        context_.metrics,
    };

    switch (message) {
    case WM_LBUTTONDOWN:
        return pointerHandler.HandleLeftButtonDown(messageWindow, lparam);
    case WM_LBUTTONDBLCLK:
        return pointerHandler.HandleLeftButtonDoubleClick(messageWindow, lparam);
    case WM_RBUTTONDOWN:
        return pointerHandler.HandleRightButtonDown(messageWindow, lparam);
    case WM_MOUSEMOVE:
        return pointerHandler.HandleMouseMove(messageWindow, wparam, lparam);
    case WM_MOUSEWHEEL:
        return pointerHandler.HandleMouseWheel(messageWindow, wparam, lparam);
    case WM_LBUTTONUP:
        return pointerHandler.HandleLeftButtonUp(messageWindow, lparam);
    case WM_RBUTTONUP:
        return 0;
    case WM_SETCURSOR:
        return pointerHandler.HandleSetCursor(messageWindow, wparam, lparam);
    default:
        return DefWindowProcW(messageWindow, message, wparam, lparam);
    }
}

} // namespace kb::editor

#endif
