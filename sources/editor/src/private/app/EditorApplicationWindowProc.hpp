#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorApplicationWindowProc {
public:
    EditorApplicationWindowProc() = delete;

#if defined(_WIN32)
    static LRESULT CALLBACK Handle(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
#endif
};

} // namespace kb::editor
