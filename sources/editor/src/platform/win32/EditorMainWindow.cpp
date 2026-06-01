#include "platform/win32/EditorMainWindow.hpp"

#if defined(_WIN32)
#include "platform/win32/Win32ErrorReporter.hpp"

#include <dwmapi.h>

namespace kb::editor {

HWND EditorMainWindow::Create(HINSTANCE instance, const wchar_t* className, const wchar_t* title, int width, int height, void* createParam) noexcept {
    constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    RECT windowRect{ 0, 0, width, height };
    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    HWND window = CreateWindowExW(
        0,
        className,
        title,
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        createParam);

    if (window == nullptr) {
        Win32ErrorReporter::PrintLastError("CreateWindowExW");
    }
    return window;
}

void EditorMainWindow::EnableDarkMode(HWND window) noexcept {
    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(window, 20, &darkMode, sizeof(darkMode));
}

} // namespace kb::editor

#endif
