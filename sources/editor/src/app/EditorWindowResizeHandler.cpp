#include "app/EditorWindowResizeHandler.hpp"

#if defined(_WIN32)
#include "app/EditorWindowResizeInteraction.hpp"

namespace kb::editor {
namespace {

constexpr UINT_PTR kResizeFrameTimerId = 0x21B0U;
constexpr UINT kResizeFrameTimerMs = 33U;

void RepaintNow(HWND window) noexcept {
    if (window == nullptr) {
        return;
    }

    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

} // namespace

LRESULT EditorWindowResizeHandler::HandleEnterSizeMove(HWND messageWindow, EditorSceneBgfxViewport& sceneViewport) {
    EditorWindowResizeInteraction::BeginWindowResize(messageWindow);
    sceneViewport.RequestPresent();
    if (messageWindow != nullptr) {
        SetTimer(messageWindow, kResizeFrameTimerId, kResizeFrameTimerMs, nullptr);
        InvalidateRect(messageWindow, nullptr, FALSE);
    }
    return 0;
}

LRESULT EditorWindowResizeHandler::HandleSize(HWND messageWindow, WPARAM wparam, LPARAM lparam, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneBgfxViewport& sceneViewport) {
    if (const auto resize = floatingWindows.Queries().ResizeEvent(messageWindow, LOWORD(lparam), HIWORD(lparam)); wparam != SIZE_MINIMIZED && resize.has_value()) {
        dockModel.Commands().ResizeFloatingPanel(resize->panelId, resize->width, resize->height);
    }

    sceneViewport.RequestPresent();
    if (messageWindow != nullptr) {
        InvalidateRect(messageWindow, nullptr, FALSE);
    }
    return 0;
}

LRESULT EditorWindowResizeHandler::HandlePlacementChanged(HWND messageWindow, EditorSceneBgfxViewport& sceneViewport) {
    EditorWindowResizeInteraction::EndWindowResize(messageWindow);
    if (messageWindow != nullptr) {
        KillTimer(messageWindow, kResizeFrameTimerId);
    }
    sceneViewport.RequestPresent();
    RepaintNow(messageWindow);
    return 0;
}

bool EditorWindowResizeHandler::HandleTimer(HWND messageWindow, WPARAM timerId, EditorSceneBgfxViewport& sceneViewport) {
    if (timerId != kResizeFrameTimerId) {
        return false;
    }
    sceneViewport.RequestPresent();
    if (messageWindow != nullptr) {
        InvalidateRect(messageWindow, nullptr, FALSE);
    }
    return true;
}

} // namespace kb::editor

#endif
