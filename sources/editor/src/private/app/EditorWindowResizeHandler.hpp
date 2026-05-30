#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorWindowResizeHandler {
public:
#if defined(_WIN32)
    static LRESULT Handle(HWND messageWindow, WPARAM wparam, LPARAM lparam, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows);
#endif
};

} // namespace kb::editor
