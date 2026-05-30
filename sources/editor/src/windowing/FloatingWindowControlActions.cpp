#include "windowing/FloatingWindowControlActions.hpp"

#if defined(_WIN32)

namespace kb::editor {

bool FloatingWindowControlActions::Execute(HWND window, FloatingWindowControlKind control) const {
    switch (control) {
    case FloatingWindowControlKind::Minimize:
        ShowWindow(window, SW_MINIMIZE);
        return true;
    case FloatingWindowControlKind::MaximizeRestore:
        ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
        return true;
    case FloatingWindowControlKind::Close:
        SendMessageW(window, WM_CLOSE, 0, 0);
        return true;
    case FloatingWindowControlKind::None:
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
