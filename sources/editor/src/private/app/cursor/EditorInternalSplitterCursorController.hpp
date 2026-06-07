#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorDockModel;
class EditorFloatingWindowManager;
class EditorSceneContext;
struct EditorMetrics;

#if defined(_WIN32)
class EditorInternalSplitterCursorController {
public:
    EditorInternalSplitterCursorController(
        HWND messageWindow,
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        const EditorSceneContext& sceneContext) noexcept;

    void UpdateCursor(int x, int y) const;
    [[nodiscard]] bool HitsResizableSplitter(int x, int y) const;

private:
    [[nodiscard]] bool HitsProjectFilesTreeSplitter(int x, int y) const;
    [[nodiscard]] bool HitsConsoleDetailSplitter(int x, int y) const;

    HWND messageWindow_ = nullptr;
    HWND mainWindow_ = nullptr;
    const EditorDockModel& dockModel_;
    const EditorFloatingWindowManager& floatingWindows_;
    const EditorMetrics& metrics_;
    const EditorSceneContext& sceneContext_;
};
#endif

} // namespace kb::editor
