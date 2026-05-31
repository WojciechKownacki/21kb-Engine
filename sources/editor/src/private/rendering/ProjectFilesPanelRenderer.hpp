#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class ProjectFilesPanelRenderer {
public:
    void Paint(HDC dc, const RECT& content, const EditorTheme& theme) const;
};

#endif

} // namespace kb::editor
