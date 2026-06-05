#include "app/EditorWindowResizeHandler.hpp"

#if defined(_WIN32)

namespace kb::editor {
namespace {

void RepaintNow(HWND window) noexcept {
    if (window == nullptr) {
        return;
    }

    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

} // namespace

LRESULT EditorWindowResizeHandler::HandleSize(HWND messageWindow, WPARAM wparam, LPARAM lparam, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows) {
    if (const auto resize = floatingWindows.Queries().ResizeEvent(messageWindow, LOWORD(lparam), HIWORD(lparam)); wparam != SIZE_MINIMIZED && resize.has_value()) {
        dockModel.Commands().ResizeFloatingPanel(resize->panelId, resize->width, resize->height);
    }

    RepaintNow(messageWindow);
    return 0;
}

LRESULT EditorWindowResizeHandler::HandlePlacementChanged(HWND messageWindow) {
    RepaintNow(messageWindow);
    return 0;
}

} // namespace kb::editor

#endif
