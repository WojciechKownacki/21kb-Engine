#include "app/EditorWindowHitTestHandler.hpp"

#if defined(_WIN32)
#include "rendering/EditorToolbarLayout.hpp"

#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr int kResizeBorder = 6;

// The main window draws its own title row, so Windows no longer knows where its edges and
// its caption are: the outer pixels resize it and the bar's empty space drags it.
[[nodiscard]] LRESULT HitTestMainWindow(HWND window, LPARAM lparam, int topBarHeight) noexcept {
    POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    ScreenToClient(window, &point);
    RECT client{};
    GetClientRect(window, &client);
    if (IsZoomed(window) == FALSE) {
        const bool left = point.x < kResizeBorder;
        const bool right = point.x >= client.right - kResizeBorder;
        const bool top = point.y < kResizeBorder;
        const bool bottom = point.y >= client.bottom - kResizeBorder;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }
    if (point.y >= 0 && point.y < topBarHeight) {
        const RECT bar{ 0, 0, client.right, topBarHeight };
        const EditorMenuRects menu = EditorToolbarLayout::ResolveMenu(bar, EditorMenuCommand::None, 0);
        const EditorToolbarRects toolbar = EditorToolbarLayout::ResolveToolbar(bar);
        if (!EditorToolbarLayout::IsInteractive(menu, toolbar, point.x, point.y)) {
            return HTCAPTION;
        }
    }
    return HTCLIENT;
}

} // namespace

LRESULT EditorWindowHitTestHandler::Handle(HWND messageWindow, LPARAM lparam, const EditorFloatingWindowManager& floatingWindows, HWND mainWindow, int topBarHeight) {
    if (floatingWindows.Queries().IsFloatingWindow(messageWindow)) {
        return floatingWindows.Queries().HitTest(messageWindow, lparam);
    }
    if (messageWindow == mainWindow) {
        return HitTestMainWindow(messageWindow, lparam, topBarHeight);
    }

    return DefWindowProcW(messageWindow, WM_NCHITTEST, 0, lparam);
}

} // namespace kb::editor

#endif
