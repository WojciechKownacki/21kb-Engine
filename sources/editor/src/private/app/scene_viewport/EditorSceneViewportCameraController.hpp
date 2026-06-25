#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorDockModel;
class EditorFloatingWindowManager;
class EditorSceneBgfxViewport;
class EditorSceneContext;
struct EditorMetrics;

#if defined(_WIN32)
class EditorSceneViewportCameraController {
public:
    EditorSceneViewportCameraController(
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport) noexcept;

    [[nodiscard]] static UINT_PTR TimerId() noexcept;

    [[nodiscard]] bool HandleLeftButtonDown(HWND messageWindow, int x, int y);
    [[nodiscard]] bool HandleRightButtonDown(HWND messageWindow, int x, int y);
    [[nodiscard]] bool HandleMiddleButtonDown(HWND messageWindow, int x, int y);
    [[nodiscard]] bool HandlePointerMove(HWND messageWindow, int x, int y);
    [[nodiscard]] bool HandleButtonUp(HWND messageWindow);
    [[nodiscard]] bool HandleMouseWheel(HWND messageWindow, int x, int y, int wheelDelta);
    [[nodiscard]] bool HandleTimer(HWND messageWindow, WPARAM timerId);
    [[nodiscard]] bool TickActiveNavigation(float deltaSeconds);
    void Cancel(HWND messageWindow) noexcept;

private:
    [[nodiscard]] bool BeginNavigation(HWND messageWindow, int x, int y, bool left, bool right, bool middle);
    [[nodiscard]] bool QueueActivePointerMove(HWND messageWindow, int x, int y);
    [[nodiscard]] bool ApplyActiveKeyboardFlight(HWND messageWindow, float deltaSeconds);
    void InvalidateActiveToolbar(HWND messageWindow) const noexcept;
    void StartCapture(HWND messageWindow, bool hideCursor) noexcept;
    void StopCapture(HWND messageWindow) noexcept;

    HWND mainWindow_ = nullptr;
    const EditorDockModel& dockModel_;
    const EditorFloatingWindowManager& floatingWindows_;
    const EditorMetrics& metrics_;
    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& sceneViewport_;
};
#endif

} // namespace kb::editor
