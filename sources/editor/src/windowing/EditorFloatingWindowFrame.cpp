#include "windowing/EditorFloatingWindowFrame.hpp"

#if defined(_WIN32)

namespace kb::editor {

LRESULT EditorFloatingWindowFrame::HandleNonClientCalcSize(HWND window, WPARAM wparam, LPARAM lparam) {
    if (lparam == 0) {
        return 0;
    }

    // Both shapes of the message carry the proposed window rect first, and both are
    // answered with the client rect. Leaving it as it arrived is what hands the whole
    // window to the editor; the frame Windows would otherwise keep is what shows up as
    // an empty bar above a torn-off panel.
    RECT* client = wparam == FALSE
        ? reinterpret_cast<RECT*>(lparam)
        : &reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam)->rgrc[0];

    // Maximizing places a window one frame outside the screen on every side, which is
    // invisible only as long as Windows owns that frame. Here it would push the panel
    // off all four edges, so a maximized window is pinned to the work area instead.
    if (IsZoomed(window) != FALSE) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE) {
            *client = monitor.rcWork;
        }
    }
    return 0;
}

} // namespace kb::editor

#endif
