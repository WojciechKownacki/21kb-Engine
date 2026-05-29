#include "EditorApplication.hpp"

#if defined(_WIN32)
#include <dwmapi.h>

#include <cstdio>

namespace kb::editor {
namespace {

constexpr wchar_t kWindowClassName[] = L"KBEditorWindow";
constexpr wchar_t kWindowTitle[] = L"21kb Engine";
constexpr int kInitialWindowWidth = 1600;
constexpr int kInitialWindowHeight = 960;

void PrintLastWin32Error(const char* action) {
    const DWORD error = GetLastError();
    char message[512]{};
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        message,
        static_cast<DWORD>(sizeof(message)),
        nullptr);

    std::fprintf(stderr, "%s failed. Win32 error %lu: %s\n", action, error, message);
}

} // namespace

EditorApplication::~EditorApplication() {
    Shutdown();
}

bool EditorApplication::Initialize() {
    instance_ = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &EditorApplication::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            PrintLastWin32Error("RegisterClassExW");
            return false;
        }
    }

    constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW;
    RECT windowRect{ 0, 0, kInitialWindowWidth, kInitialWindowHeight };
    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance_,
        this);

    if (window_ == nullptr) {
        PrintLastWin32Error("CreateWindowExW");
        return false;
    }

    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(window_, 20, &darkMode, sizeof(darkMode));

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);

    running_ = true;
    return true;
}

void EditorApplication::Run() {
    MSG message{};
    while (running_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void EditorApplication::Shutdown() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (instance_ != nullptr) {
        UnregisterClassW(kWindowClassName, instance_);
        instance_ = nullptr;
    }

    running_ = false;
}

LRESULT CALLBACK EditorApplication::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    EditorApplication* app = nullptr;

    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<EditorApplication*>(createStruct->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<EditorApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (app != nullptr) {
        return app->HandleWindowMessage(message, wparam, lparam);
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT EditorApplication::HandleWindowMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_SIZE:
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        running_ = false;
        DestroyWindow(window_);
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        if (window_ != nullptr) {
            window_ = nullptr;
            running_ = false;
            PostQuitMessage(0);
        }
        return 0;
    default:
        return DefWindowProcW(window_, message, wparam, lparam);
    }
}

void EditorApplication::Paint() {
    renderer_.Paint(window_, dockModel_, theme_, metrics_);
}

} // namespace kb::editor

#else

namespace kb::editor {

EditorApplication::~EditorApplication() = default;

bool EditorApplication::Initialize() {
    return false;
}

void EditorApplication::Run() {
}

void EditorApplication::Shutdown() {
}

} // namespace kb::editor

#endif
