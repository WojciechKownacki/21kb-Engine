#include "windowing/FloatingWindowHitTestResolver.hpp"

#if defined(_WIN32)
#include "windowing/FloatingWindowControlHitTester.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

#include <windowsx.h>

namespace kb::editor {

LRESULT FloatingWindowHitTestResolver::Resolve(HWND window, LPARAM lparam, const EditorMetrics& metrics) {
    POINT screenPoint{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    RECT frame{};
    GetWindowRect(window, &frame);

    const int x = screenPoint.x - frame.left;
    const int y = screenPoint.y - frame.top;
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const int border = metrics.floatingResizeBorder;

    const bool left = x >= 0 && x < border;
    const bool right = x >= width - border && x < width;
    const bool top = y >= 0 && y < border;
    const bool bottom = y >= height - border && y < height;

    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }

    POINT clientPoint = screenPoint;
    ScreenToClient(window, &clientPoint);
    RECT client{};
    GetClientRect(window, &client);

    const int controlsLeft = client.right - FloatingWindowControlLayout::TotalWidth(metrics);
    if (FloatingWindowControlHitTester{}.HitTest(metrics, client.right, clientPoint.x, clientPoint.y) != FloatingWindowControlKind::None) {
        return HTCLIENT;
    }
    if (clientPoint.y >= 0 && clientPoint.y < metrics.tabStripHeight && clientPoint.x >= metrics.tabWidth && clientPoint.x < controlsLeft) {
        return HTCAPTION;
    }

    return HTCLIENT;
}

} // namespace kb::editor

#endif
