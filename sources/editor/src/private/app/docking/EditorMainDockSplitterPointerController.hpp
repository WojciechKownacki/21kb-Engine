#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorDockController;
class EditorDockModel;
class EditorSceneContext;
class EditorSceneBgfxViewport;
struct EditorMetrics;

#if defined(_WIN32)
class EditorMainDockSplitterPointerController {
public:
    EditorMainDockSplitterPointerController(
        HWND mainWindow,
        EditorDockModel& dockModel,
        EditorDockController& dockController,
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport,
        const EditorMetrics& metrics) noexcept;

    [[nodiscard]] bool HandlePointerDown(HWND messageWindow, int x, int y);

private:
    [[nodiscard]] bool HitsMainSplitter(HWND messageWindow, int x, int y) const;

    HWND mainWindow_ = nullptr;
    EditorDockModel& dockModel_;
    EditorDockController& dockController_;
    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& sceneViewport_;
    const EditorMetrics& metrics_;
};
#endif

} // namespace kb::editor
