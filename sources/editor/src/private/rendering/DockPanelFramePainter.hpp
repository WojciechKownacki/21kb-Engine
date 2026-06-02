#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class DockPanelFramePainter {
public:
    void Paint(HDC dc, const RECT& rect, DockPanelKind kind, const EditorTheme& theme) const;

private:
    static void PaintTransparentContentFrame(HDC dc, const RECT& rect, const EditorTheme& theme);
};

#endif

} // namespace kb::editor
