#include "app/EditorWindowMessageRouter.hpp"

#if defined(_WIN32)
#include "app/EditorHierarchySearchInputHandler.hpp"
#include "app/EditorAssetBrowserInputHandler.hpp"
#include "app/EditorWindowHitTestHandler.hpp"
#include "app/EditorWindowLifecycleHandler.hpp"
#include "app/EditorPaintDispatcher.hpp"
#include "app/EditorWindowPointerMessageDispatcher.hpp"
#include "app/EditorWindowResizeHandler.hpp"

namespace kb::editor {

EditorWindowMessageRouter::EditorWindowMessageRouter(EditorWindowMessageContext context) noexcept
    : context_(context) {}

LRESULT EditorWindowMessageRouter::Handle(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        EditorPaintDispatcher{
            context_.mainWindow,
            context_.dockModel,
            context_.sceneContext,
            context_.theme,
            context_.metrics,
            context_.renderer,
            context_.renderBackendSettings,
            context_.playMode,
            context_.shellInteraction,
            context_.sceneViewport,
            context_.floatingWindows,
            context_.dockController,
            context_.pointerDrag,
        }.Paint(messageWindow);
        return 0;
    case WM_SIZE:
        return EditorWindowResizeHandler::Handle(messageWindow, wparam, lparam, context_.dockModel, context_.floatingWindows);
    case WM_CHAR:
        if (EditorAssetBrowserInputHandler{ context_.mainWindow, context_.sceneContext }.HandleChar(messageWindow, wparam)) {
            return 0;
        }
        if (EditorHierarchySearchInputHandler{ context_.mainWindow, context_.sceneContext }.HandleChar(messageWindow, wparam)) {
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (EditorAssetBrowserInputHandler{ context_.mainWindow, context_.sceneContext }.HandleKeyDown(messageWindow, wparam)) {
            return 0;
        }
        if (EditorHierarchySearchInputHandler{ context_.mainWindow, context_.sceneContext }.HandleKeyDown(messageWindow, wparam)) {
            return 0;
        }
        break;
    case WM_NCHITTEST:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_SETCURSOR:
        if (message == WM_NCHITTEST) {
            return EditorWindowHitTestHandler::Handle(messageWindow, lparam, context_.floatingWindows);
        }
        return EditorWindowPointerMessageDispatcher{ context_ }.Dispatch(messageWindow, message, wparam, lparam);
    case WM_CONTEXTMENU:
        return 0;
    case WM_CLOSE:
        return EditorWindowLifecycleHandler{ context_.mainWindow, context_.running, context_.dockModel, context_.floatingWindows }.HandleClose(messageWindow);
    case WM_DESTROY:
        return EditorWindowLifecycleHandler{ context_.mainWindow, context_.running, context_.dockModel, context_.floatingWindows }.HandleDestroy(messageWindow);
    default:
        break;
    }

    return DefWindowProcW(messageWindow, message, wparam, lparam);
}

} // namespace kb::editor

#endif
