#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

class EditorDockModel;
class EditorFloatingWindowManager;
class EditorSceneBgfxViewport;
class EditorSceneContext;
struct EditorMetrics;

#if defined(_WIN32)
// The pointer input of the Static Mesh window: buttons, sliders and level cells of its panel, and the orbit, pan
// and zoom of its preview. Every handler answers whether the window took the input.
class StaticMeshEditorPointerController {
public:
    StaticMeshEditorPointerController(
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport) noexcept;

    [[nodiscard]] bool HandleLeftButtonDown(HWND messageWindow, int x, int y);
    [[nodiscard]] bool HandleRightButtonDown(HWND messageWindow, int x, int y);
    [[nodiscard]] bool HandleMiddleButtonDown(HWND messageWindow, int x, int y);
    [[nodiscard]] bool HandleMouseMove(HWND messageWindow, int x, int y);
    [[nodiscard]] bool HandleMouseWheel(HWND messageWindow, int x, int y, int wheelDelta);
    // Ends a camera navigation or a slider drag.
    [[nodiscard]] bool HandleButtonUp(HWND messageWindow);

private:
    [[nodiscard]] std::optional<RECT> Content(HWND messageWindow) const;
    [[nodiscard]] bool BeginNavigation(HWND messageWindow, int x, int y, bool left, bool right, bool middle);
    void Refresh(HWND messageWindow) const noexcept;

    HWND mainWindow_ = nullptr;
    const EditorDockModel& dockModel_;
    const EditorFloatingWindowManager& floatingWindows_;
    const EditorMetrics& metrics_;
    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& sceneViewport_;
};
#endif

} // namespace kb::editor
