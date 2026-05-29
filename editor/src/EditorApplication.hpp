#pragma once

#include "EditorDockModel.hpp"
#include "EditorGdiRenderer.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
#endif

    EditorDockModel dockModel_;
    EditorTheme theme_ = MakeVerthDarkTheme();
    EditorMetrics metrics_;
    EditorGdiRenderer renderer_;
    bool running_ = false;
};

} // namespace kb::editor
