#pragma once

#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockMainLayoutResolver {
public:
    DockMainLayoutResolver() = delete;

#if defined(_WIN32)
    [[nodiscard]] static DockLayout Resolve(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics);
#endif
};

} // namespace kb::editor
