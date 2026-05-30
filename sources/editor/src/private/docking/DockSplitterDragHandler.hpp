#pragma once

#include "docking/DockPointerDrag.hpp"
#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockSplitterDragHandler {
public:
    DockSplitterDragHandler() = delete;

#if defined(_WIN32)
    static void Move(DockPointerDrag& drag, int x, int y, HWND mainWindow, EditorDockModel& dockModel, const EditorMetrics& metrics);
#endif
};

} // namespace kb::editor
