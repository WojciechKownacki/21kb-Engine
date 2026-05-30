#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "windowing/FloatingWindowControlActions.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class FloatingWindowControlInteractor {
public:
#if defined(_WIN32)
    [[nodiscard]] bool HandlePointerDown(HWND window, const EditorMetrics& metrics, int x, int y) const;
#endif
};

} // namespace kb::editor
