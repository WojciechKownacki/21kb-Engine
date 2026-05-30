#include "app/EditorWindowMessageRouter.hpp"

#if defined(_WIN32)
#include "app/EditorPaintDispatcher.hpp"

#include "kb/editor/docking/DockTypes.hpp"

#include <windowsx.h>

namespace kb::editor {

EditorWindowMessageRouter::EditorWindowMessageRouter(
    HWND& mainWindow,
    bool& running,
    EditorDockModel& dockModel,
    EditorSceneContext& sceneContext,
    EditorTheme& theme,
    EditorMetrics& metrics,
    EditorGdiRenderer& renderer,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorHierarchySelectionController& hierarchySelection) noexcept
    : mainWindow_(mainWindow)
    , running_(running)
    , dockModel_(dockModel)
    , sceneContext_(sceneContext)
    , theme_(theme)
    , metrics_(metrics)
    , renderer_(renderer)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , hierarchySelection_(hierarchySelection) {}

LRESULT EditorWindowMessageRouter::Handle(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(messageWindow);
        return 0;
    case WM_SIZE:
        if (const auto resize = floatingWindows_.OnResized(messageWindow, LOWORD(lparam), HIWORD(lparam)); wparam != SIZE_MINIMIZED && resize.has_value()) {
            dockModel_.ResizeFloatingPanel(resize->panelId, resize->width, resize->height);
        }
        InvalidateRect(messageWindow, nullptr, FALSE);
        return 0;
    case WM_NCHITTEST:
        if (floatingWindows_.IsFloatingWindow(messageWindow)) {
            return floatingWindows_.HitTest(messageWindow, lparam);
        }
        break;
    case WM_LBUTTONDOWN:
        if (hierarchySelection_.HandlePointerDown(messageWindow, mainWindow_, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), dockModel_, floatingWindows_, metrics_, sceneContext_)) {
            InvalidateRect(mainWindow_, nullptr, FALSE);
            if (!IsMainWindow(messageWindow)) {
                InvalidateRect(messageWindow, nullptr, FALSE);
            }
            return 0;
        }
        dockController_.HandlePointerDown(messageWindow, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_MOUSEMOVE:
        dockController_.HandlePointerMove(messageWindow, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_LBUTTONUP:
        dockController_.HandlePointerUp(messageWindow);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(messageWindow, &point);
            dockController_.UpdateHoverCursor(messageWindow, point.x, point.y);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        if (const std::uint32_t panelId = floatingWindows_.PanelId(messageWindow); panelId != 0) {
            floatingWindows_.Destroy(panelId);
            dockModel_.DockPanelTo(panelId, DockDropPreview{ .zone = DockDropZone::Bottom });
            InvalidateRect(mainWindow_, nullptr, FALSE);
        } else {
            running_ = false;
            DestroyWindow(mainWindow_);
            mainWindow_ = nullptr;
            PostQuitMessage(0);
        }
        return 0;
    case WM_DESTROY:
        if (floatingWindows_.IsFloatingWindow(messageWindow)) {
            floatingWindows_.OnDestroyed(messageWindow);
            return 0;
        }
        if (mainWindow_ != nullptr && messageWindow == mainWindow_) {
            mainWindow_ = nullptr;
            running_ = false;
            PostQuitMessage(0);
        }
        return 0;
    default:
        return DefWindowProcW(messageWindow, message, wparam, lparam);
    }

    return DefWindowProcW(messageWindow, message, wparam, lparam);
}

bool EditorWindowMessageRouter::IsMainWindow(HWND candidate) const noexcept {
    return candidate == mainWindow_;
}

void EditorWindowMessageRouter::Paint(HWND paintWindow) const {
    EditorPaintDispatcher{ mainWindow_, dockModel_, sceneContext_, theme_, metrics_, renderer_, floatingWindows_, dockController_ }.Paint(paintWindow);
}

} // namespace kb::editor

#endif
