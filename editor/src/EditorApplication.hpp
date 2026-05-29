#pragma once

#include "EditorDockModel.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace kb::editor {

class EditorApplication {
public:
    EditorApplication() = default;
    ~EditorApplication();

    EditorApplication(const EditorApplication&) = delete;
    EditorApplication& operator=(const EditorApplication&) = delete;
    EditorApplication(EditorApplication&&) = delete;
    EditorApplication& operator=(EditorApplication&&) = delete;

    bool Initialize();
    void Run();
    void Shutdown();

private:
#if defined(_WIN32)
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT HandleWindowMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void Paint();
    void DrawPanel(HDC dc, const RECT& rect, const char* title, COLORREF fill) const;
    void DrawTextLine(HDC dc, const RECT& rect, const char* text, COLORREF color) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
#endif

    EditorDockModel dockModel_;
    bool running_ = false;
};

} // namespace kb::editor
