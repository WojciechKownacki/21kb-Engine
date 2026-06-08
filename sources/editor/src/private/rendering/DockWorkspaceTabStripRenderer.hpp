#pragma once

#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockWorkspaceTabStripRenderer {
public:
#if defined(_WIN32)
    void Paint(HWND owner, HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) const;
#endif
};

} // namespace kb::editor
