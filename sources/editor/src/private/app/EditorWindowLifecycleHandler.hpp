#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

class EditorWindowLifecycleHandler {
public:
#if defined(_WIN32)
    EditorWindowLifecycleHandler(HWND& mainWindow, bool& running, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext) noexcept;

    LRESULT HandleClose(HWND messageWindow);
    LRESULT HandleDestroy(HWND messageWindow);
#endif

private:
#if defined(_WIN32)
    HWND& mainWindow_;
    bool& running_;
    EditorDockModel& dockModel_;
    EditorFloatingWindowManager& floatingWindows_;
    EditorSceneContext& sceneContext_;
#endif
};

} // namespace kb::editor
