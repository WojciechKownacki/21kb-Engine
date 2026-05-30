#pragma once

#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockWorkspaceChromeRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) const;
#endif

private:
#if defined(_WIN32)
    static void PaintSplitters(HDC dc, const DockLayout& layout, const EditorTheme& theme);
    static void PaintLeaves(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme);
#endif
};

} // namespace kb::editor
