#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorWindowResizeHandler {
public:
#if defined(_WIN32)
    static LRESULT HandleEnterSizeMove(HWND messageWindow, EditorSceneBgfxViewport& sceneViewport);
    static LRESULT HandleSize(HWND messageWindow, WPARAM wparam, LPARAM lparam, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneBgfxViewport& sceneViewport);
    static LRESULT HandlePlacementChanged(HWND messageWindow, EditorSceneBgfxViewport& sceneViewport);
    static bool HandleTimer(HWND messageWindow, WPARAM timerId, EditorSceneBgfxViewport& sceneViewport);
#endif
};

} // namespace kb::editor
