#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace kb::editor {

class EditorMainWindow {
public:
    EditorMainWindow() = delete;

    [[nodiscard]] static HWND Create(HINSTANCE instance, const wchar_t* className, const wchar_t* title, int width, int height, void* createParam) noexcept;
    static void EnableDarkMode(HWND window) noexcept;
};

} // namespace kb::editor

#endif
