#pragma once

#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockHoverCursorUpdater {
public:
    DockHoverCursorUpdater() = delete;

#if defined(_WIN32)
    static void Update(HWND window, int x, int y, HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics);
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] static bool IsMainWindow(HWND window, HWND mainWindow) noexcept;
#endif
};

} // namespace kb::editor
