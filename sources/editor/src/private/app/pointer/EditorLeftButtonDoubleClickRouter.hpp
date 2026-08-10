#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)
class EditorLeftButtonDoubleClickRouter {
public:
    EditorLeftButtonDoubleClickRouter(
        HWND mainWindow,
        EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport,
        const EditorMetrics& metrics) noexcept;

    [[nodiscard]] bool Handle(HWND messageWindow, int x, int y);

private:
    HWND mainWindow_ = nullptr;
    EditorDockModel& dockModel_;
    const EditorFloatingWindowManager& floatingWindows_;
    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& sceneViewport_;
    const EditorMetrics& metrics_;
};
#endif

} // namespace kb::editor
