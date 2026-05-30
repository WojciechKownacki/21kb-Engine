#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class FloatingWindowControlRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const RECT& client, const EditorTheme& theme, const EditorMetrics& metrics) const;
#endif
};

} // namespace kb::editor
