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
class EditorMouseWheelRouter {
public:
    EditorMouseWheelRouter(
        HWND messageWindow,
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport) noexcept;

    [[nodiscard]] bool HandleMouseWheel(int x, int y, int wheelDelta);

private:
    HWND messageWindow_ = nullptr;
    HWND mainWindow_ = nullptr;
    const EditorDockModel& dockModel_;
    const EditorFloatingWindowManager& floatingWindows_;
    const EditorMetrics& metrics_;
    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& sceneViewport_;
};
#endif

} // namespace kb::editor
