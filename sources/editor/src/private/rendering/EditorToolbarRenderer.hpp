#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorToolbarRenderer {
public:
#if defined(_WIN32)
    void PaintMenu(HDC dc, const RECT& rect, const EditorTheme& theme) const;
    void PaintToolbar(HDC dc, const RECT& rect, const EditorTheme& theme) const;
#endif

private:
#if defined(_WIN32)
    void PaintButton(HDC dc, const RECT& rect, const char* label, const EditorTheme& theme, bool active) const;
#endif
};

} // namespace kb::editor
