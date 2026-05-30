#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class FloatingWindowHitTestResolver {
public:
    FloatingWindowHitTestResolver() = delete;

#if defined(_WIN32)
    [[nodiscard]] static LRESULT Resolve(HWND window, LPARAM lparam, const EditorMetrics& metrics);
#endif
};

} // namespace kb::editor
