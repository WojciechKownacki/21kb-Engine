#include "kb/editor/EditorApplication.hpp"

#if defined(_WIN32)
#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "rendering/EditorGdiRenderer.hpp"

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#include <dwmapi.h>
#include <windowsx.h>

#include <cstdio>
#include <memory>

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

struct EditorApplication::Impl {
    bool Initialize();
    void Run();
    void Shutdown();

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT HandleWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void Paint(HWND window);
    [[nodiscard]] bool IsMainWindow(HWND window) const noexcept;

    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    EditorDockModel dockModel;
    EditorTheme theme = MakeEditorDarkTheme();
    EditorMetrics metrics;
    EditorGdiRenderer renderer;
    EditorFloatingWindowManager floatingWindows;
    EditorDockController dockController;
    bool running = false;
};

EditorApplication::EditorApplication()
    : impl_(std::make_unique<Impl>()) {
}

EditorApplication::~EditorApplication() {
    Shutdown();
}

bool EditorApplication::Initialize() {
    return impl_->Initialize();
}

void EditorApplication::Run() {
    impl_->Run();
}

void EditorApplication::Shutdown() {
    if (impl_ != nullptr) {
        impl_->Shutdown();
    }
}

bool EditorApplication::Impl::Initialize() {
    instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &EditorApplication::Impl::WindowProc;
    windowClass.hInstance = instance;
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

    windowClass.lpszClassName = EditorFloatingWindowManager::WindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            PrintLastWin32Error("RegisterClassExW floating");
            return false;
        }
    }

    constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW;
    RECT windowRect{ 0, 0, kInitialWindowWidth, kInitialWindowHeight };
    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    window = CreateWindowExW(
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
        instance,
        this);

    if (window == nullptr) {
        PrintLastWin32Error("CreateWindowExW");
        return false;
    }

    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(window, 20, &darkMode, sizeof(darkMode));
    floatingWindows.Configure(instance, window, metrics);
    dockController.Configure(window, dockModel, floatingWindows, metrics);

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    running = true;
    return true;
}

void EditorApplication::Impl::Run() {
    MSG message{};
    while (running && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void EditorApplication::Impl::Shutdown() {
    floatingWindows.Shutdown();

    if (window != nullptr) {
        DestroyWindow(window);
        window = nullptr;
    }

    if (instance != nullptr) {
        UnregisterClassW(EditorFloatingWindowManager::WindowClassName, instance);
        UnregisterClassW(kWindowClassName, instance);
        instance = nullptr;
    }

    running = false;
}

LRESULT CALLBACK EditorApplication::Impl::WindowProc(HWND windowHandle, UINT message, WPARAM wparam, LPARAM lparam) {
    Impl* app = nullptr;

    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<Impl*>(createStruct->lpCreateParams);
        if (app != nullptr && app->window == nullptr) {
            app->window = windowHandle;
        }
        if (app != nullptr) {
            SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
    } else {
        app = reinterpret_cast<Impl*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
    }

    if (app != nullptr) {
        return app->HandleWindowMessage(windowHandle, message, wparam, lparam);
    }

    return DefWindowProcW(windowHandle, message, wparam, lparam);
}

LRESULT EditorApplication::Impl::HandleWindowMessage(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(messageWindow);
        return 0;
    case WM_SIZE:
        if (const auto resize = floatingWindows.OnResized(messageWindow, LOWORD(lparam), HIWORD(lparam)); wparam != SIZE_MINIMIZED && resize.has_value()) {
            dockModel.ResizeFloatingPanel(resize->panelId, resize->width, resize->height);
        }
        InvalidateRect(messageWindow, nullptr, FALSE);
        return 0;
    case WM_NCHITTEST:
        if (floatingWindows.IsFloatingWindow(messageWindow)) {
            return floatingWindows.HitTest(messageWindow, lparam);
        }
        break;
    case WM_LBUTTONDOWN:
        dockController.HandlePointerDown(messageWindow, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_MOUSEMOVE:
        dockController.HandlePointerMove(messageWindow, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_LBUTTONUP:
        dockController.HandlePointerUp(messageWindow);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(messageWindow, &point);
            dockController.UpdateHoverCursor(messageWindow, point.x, point.y);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        if (const std::uint32_t panelId = floatingWindows.PanelId(messageWindow); panelId != 0) {
            floatingWindows.Destroy(panelId);
            dockModel.DockPanelTo(panelId, DockDropPreview{ .zone = DockDropZone::Bottom });
            InvalidateRect(window, nullptr, FALSE);
        } else {
            running = false;
            DestroyWindow(window);
            window = nullptr;
            PostQuitMessage(0);
        }
        return 0;
    case WM_DESTROY:
        if (floatingWindows.IsFloatingWindow(messageWindow)) {
            floatingWindows.OnDestroyed(messageWindow);
            return 0;
        }
        if (window != nullptr && messageWindow == window) {
            window = nullptr;
            running = false;
            PostQuitMessage(0);
        }
        return 0;
    default:
        return DefWindowProcW(messageWindow, message, wparam, lparam);
    }

    return DefWindowProcW(messageWindow, message, wparam, lparam);
}

void EditorApplication::Impl::Paint(HWND paintWindow) {
    if (paintWindow == nullptr || IsMainWindow(paintWindow)) {
        renderer.Paint(window, dockModel, theme, metrics, dockController.DropPreview());
        return;
    }

    if (const DockPanel* panel = dockModel.FindPanel(floatingWindows.PanelId(paintWindow)); panel != nullptr) {
        renderer.PaintFloating(paintWindow, *panel, theme, metrics);
    }
}

bool EditorApplication::Impl::IsMainWindow(HWND candidate) const noexcept {
    return candidate == window;
}

} // namespace kb::editor

#else

#include <memory>

namespace kb::editor {

struct EditorApplication::Impl {
};

EditorApplication::EditorApplication()
    : impl_(std::make_unique<Impl>()) {
}

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
