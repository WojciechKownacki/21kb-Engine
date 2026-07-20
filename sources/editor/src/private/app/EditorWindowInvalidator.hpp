#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorWindowInvalidator {
public:
    EditorWindowInvalidator() = delete;

#if defined(_WIN32)
    static void InvalidateMainAndSource(HWND mainWindow, HWND sourceWindow) noexcept;
    static void InvalidatePanel(HWND window, const RECT& panelRect) noexcept;
#endif
};

} // namespace kb::editor
