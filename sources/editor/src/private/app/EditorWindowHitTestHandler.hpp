#pragma once

#include "docking/EditorFloatingWindowManager.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorWindowHitTestHandler {
public:
#if defined(_WIN32)
    [[nodiscard]] static LRESULT Handle(HWND messageWindow, LPARAM lparam, const EditorFloatingWindowManager& floatingWindows);
#endif
};

} // namespace kb::editor
