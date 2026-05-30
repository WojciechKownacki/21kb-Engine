#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockPanelChromeRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const RECT& rect, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, bool active) const;
#endif
};

} // namespace kb::editor
