#include "app/EditorWindowMessageRouter.hpp"

#if defined(_WIN32)
#include "app/EditorEditCommandInputHandler.hpp"
#include "app/EditorHierarchySearchInputHandler.hpp"
#include "app/EditorAssetBrowserInputHandler.hpp"
#include "app/EditorWindowHitTestHandler.hpp"
#include "app/EditorWindowLifecycleHandler.hpp"
#include "app/EditorPaintDispatcher.hpp"
#include "app/EditorWindowPointerMessageDispatcher.hpp"
#include "app/EditorWindowResizeHandler.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "inspection/InspectorPanelInteraction.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool ModifierDown(int virtualKey) noexcept {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] bool HandleTransformToolShortcut(EditorSceneContext& sceneContext, WPARAM key) noexcept {
    if (ModifierDown(VK_CONTROL) || ModifierDown(VK_MENU) || sceneContext.HasActiveViewportCameraNavigation() || sceneContext.Gizmo().IsDragging()) {
        return false;
    }

    EditorTransformToolMode mode = sceneContext.Gizmo().toolMode;
    switch (key) {
    case 'W':
        mode = EditorTransformToolMode::Translate;
        break;
    case 'E':
        mode = EditorTransformToolMode::Rotate;
        break;
    case 'R':
        mode = EditorTransformToolMode::Scale;
        break;
    default:
        return false;
    }

    EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    if (gizmo.toolMode == mode) {
        return true;
    }
    gizmo.toolMode = mode;
    gizmo.hoveredAxis = -1;
    gizmo.draggedAxis = -1;
    sceneContext.MarkSceneRenderDirty();
    return true;
}

} // namespace

EditorWindowMessageRouter::EditorWindowMessageRouter(EditorWindowMessageContext context) noexcept
    : context_(context) {}

LRESULT EditorWindowMessageRouter::Handle(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        HDC controlDc = reinterpret_cast<HDC>(wparam);
        SetTextColor(controlDc, RGB(196, 205, 214));
        SetBkColor(controlDc, RGB(20, 22, 24));
        static HBRUSH consoleDetailBrush = CreateSolidBrush(RGB(20, 22, 24));
        return reinterpret_cast<LRESULT>(consoleDetailBrush);
    }
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
        context_.sceneViewport.RequestPresent();
        return EditorWindowResizeHandler::HandleSize(messageWindow, wparam, lparam, context_.dockModel, context_.floatingWindows);
    case WM_EXITSIZEMOVE:
        context_.sceneViewport.RequestPresent();
        return EditorWindowResizeHandler::HandlePlacementChanged(messageWindow);
    case WM_CANCELMODE:
        EditorSceneViewportCameraController{
            context_.mainWindow,
            context_.dockModel,
            context_.floatingWindows,
            context_.metrics,
            context_.sceneContext,
            context_.sceneViewport,
        }.Cancel(messageWindow);
        static_cast<void>(EditorSceneViewportObjectInteraction::CancelGizmoDrag(context_.sceneContext));
        context_.dockController.CancelDrag();
        context_.pointerDrag.Clear();
        context_.shellInteraction.ClearPressedSave();
        context_.shellInteraction.ClearPressedTransport();
        context_.sceneViewport.RequestPresent();
        return 0;
    case WM_CAPTURECHANGED:
    {
        HWND newCapture = reinterpret_cast<HWND>(lparam);
        context_.dockController.HandleCaptureChanged(newCapture);
        if (newCapture != messageWindow) {
            EditorSceneViewportCameraController{
                context_.mainWindow,
                context_.dockModel,
                context_.floatingWindows,
                context_.metrics,
                context_.sceneContext,
                context_.sceneViewport,
            }.Cancel(messageWindow);
            static_cast<void>(EditorSceneViewportObjectInteraction::CancelGizmoDrag(context_.sceneContext));
        }
        const bool captureStayedInEditor = newCapture == context_.mainWindow || context_.floatingWindows.Queries().IsFloatingWindow(newCapture);
        if (!captureStayedInEditor) {
            context_.pointerDrag.Clear();
            context_.shellInteraction.ClearPressedSave();
            context_.shellInteraction.ClearPressedTransport();
            if (context_.mainWindow != nullptr) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(messageWindow, nullptr, FALSE);
            }
        }
        context_.sceneViewport.RequestPresent();
        return 0;
    }
    case WM_CHAR:
        if (InspectorPanelInteraction::HandleChar(context_.sceneContext, static_cast<wchar_t>(wparam))) {
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        if (EditorAssetBrowserInputHandler{ context_.mainWindow, context_.sceneContext }.HandleChar(messageWindow, wparam)) {
            return 0;
        }
        if (EditorHierarchySearchInputHandler{ context_.mainWindow, context_.sceneContext }.HandleChar(messageWindow, wparam)) {
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (InspectorPanelInteraction::HandleKeyCapture(context_.sceneContext, wparam)) {
            context_.sceneViewport.RequestPresent();
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        if (InspectorPanelInteraction::HandleKeyDown(messageWindow, context_.sceneContext, wparam)) {
            context_.sceneViewport.RequestPresent();
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        if (EditorAssetBrowserInputHandler{ context_.mainWindow, context_.sceneContext }.HandleKeyDown(messageWindow, wparam)) {
            return 0;
        }
        if (EditorHierarchySearchInputHandler{ context_.mainWindow, context_.sceneContext }.HandleKeyDown(messageWindow, wparam)) {
            context_.sceneViewport.RequestPresent();
            return 0;
        }
        if (HandleTransformToolShortcut(context_.sceneContext, wparam)) {
            context_.sceneViewport.RequestPresent();
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        if (EditorEditCommandInputHandler{ context_.sceneContext }.HandleKeyDown(wparam)) {
            context_.sceneViewport.RequestPresent();
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_TIMER:
        if (EditorSceneViewportCameraController{
                context_.mainWindow,
                context_.dockModel,
                context_.floatingWindows,
                context_.metrics,
                context_.sceneContext,
                context_.sceneViewport,
            }.HandleTimer(messageWindow, wparam)) {
            return 0;
        }
        break;
    case WM_NCHITTEST:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_SETCURSOR:
        if (message == WM_NCHITTEST) {
            return EditorWindowHitTestHandler::Handle(messageWindow, lparam, context_.floatingWindows);
        }
        if (context_.sceneContext.Inspector().IsListeningForKey() &&
            (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN)) {
            const int captureButton = message == WM_LBUTTONDOWN ? VK_LBUTTON : (message == WM_RBUTTONDOWN ? VK_RBUTTON : VK_MBUTTON);
            static_cast<void>(InspectorPanelInteraction::HandleKeyCapture(context_.sceneContext, static_cast<WPARAM>(captureButton)));
            InvalidateRect(messageWindow, nullptr, FALSE);
            if (messageWindow != context_.mainWindow) {
                InvalidateRect(context_.mainWindow, nullptr, FALSE);
            }
            return 0;
        }
        return EditorWindowPointerMessageDispatcher{ context_ }.Dispatch(messageWindow, message, wparam, lparam);
    case WM_CONTEXTMENU:
        return 0;
    case WM_CLOSE:
        return EditorWindowLifecycleHandler{ context_.mainWindow, context_.running, context_.dockModel, context_.floatingWindows, context_.sceneContext }.HandleClose(messageWindow);
    case WM_DESTROY:
        return EditorWindowLifecycleHandler{ context_.mainWindow, context_.running, context_.dockModel, context_.floatingWindows, context_.sceneContext }.HandleDestroy(messageWindow);
    default:
        break;
    }

    return DefWindowProcW(messageWindow, message, wparam, lparam);
}

} // namespace kb::editor

#endif
