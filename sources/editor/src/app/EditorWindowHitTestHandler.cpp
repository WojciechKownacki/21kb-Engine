#include "app/EditorWindowHitTestHandler.hpp"

#if defined(_WIN32)

namespace kb::editor {

LRESULT EditorWindowHitTestHandler::Handle(HWND messageWindow, LPARAM lparam, const EditorFloatingWindowManager& floatingWindows) {
    if (floatingWindows.Queries().IsFloatingWindow(messageWindow)) {
        return floatingWindows.Queries().HitTest(messageWindow, lparam);
    }

    return DefWindowProcW(messageWindow, WM_NCHITTEST, 0, lparam);
}

} // namespace kb::editor

#endif
