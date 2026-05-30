#include "app/EditorWindowResizeHandler.hpp"

#if defined(_WIN32)

namespace kb::editor {

LRESULT EditorWindowResizeHandler::Handle(HWND messageWindow, WPARAM wparam, LPARAM lparam, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows) {
    if (const auto resize = floatingWindows.Queries().ResizeEvent(messageWindow, LOWORD(lparam), HIWORD(lparam)); wparam != SIZE_MINIMIZED && resize.has_value()) {
        dockModel.Commands().ResizeFloatingPanel(resize->panelId, resize->width, resize->height);
    }

    InvalidateRect(messageWindow, nullptr, FALSE);
    return 0;
}

} // namespace kb::editor

#endif
