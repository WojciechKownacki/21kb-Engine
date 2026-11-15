#include "platform/win32/EditorSplashWindow.hpp"

#if defined(_WIN32)
#include "app/EditorStartupProgress.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorSplashPainter.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dwmapi.h>

#include <future>
#include <mutex>
#include <thread>
#include <utility>

namespace kb::editor {
namespace {

constexpr wchar_t kClassName[] = L"KBEditorStartupSplash";
constexpr wchar_t kWindowTitle[] = L"21kb Engine";
constexpr UINT kStateChanged = WM_APP + 1U;
constexpr UINT kCloseNow = WM_APP + 2U;
// DWMWA_WINDOW_CORNER_PREFERENCE and DWMWCP_ROUND: rounded corners on Windows 11, ignored before.
constexpr DWORD kCornerPreference = 33U;
constexpr int kRoundCorners = 2;

} // namespace

struct EditorSplashWindow::Impl {
    std::thread thread;
    // Guards `snapshot`, which the main thread writes and the splash thread paints.
    std::mutex mutex;
    EditorStartupSnapshot snapshot;
    // Written by the splash thread before it reports ready, read by the main thread afterwards.
    HWND window = nullptr;
    int dpi = 96;

    void Paint(HDC target) {
        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0) {
            return;
        }
        EditorStartupSnapshot shown;
        {
            const std::lock_guard<std::mutex> lock{ mutex };
            shown = snapshot;
        }
        // One blit, so the bar and text never flicker while they are repainted.
        HDC memory = CreateCompatibleDC(target);
        HBITMAP bitmap = memory == nullptr ? nullptr : CreateCompatibleBitmap(target, width, height);
        if (bitmap != nullptr) {
            const HGDIOBJ previous = SelectObject(memory, bitmap);
            EditorSplashPainter::Paint(memory, EditorSplashPainter::Layout(dpi), MakeEditorDarkTheme(), shown);
            BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory != nullptr) {
            DeleteDC(memory);
        }
    }

    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            if (self != nullptr && dc != nullptr) {
                self->Paint(dc);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_PRINTCLIENT:
            if (self != nullptr) {
                self->Paint(reinterpret_cast<HDC>(wparam));
            }
            return 0;
        case kStateChanged:
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            // Only Close() ends the screen; a stray Alt+F4 must not.
            return 0;
        case kCloseNow:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wparam, lparam);
        }
    }

    // The splash thread: creates the window, reports whether it is up, then runs its message loop.
    void Run(std::promise<bool> ready) {
        // This thread's windows are per-monitor DPI aware, whatever the editor process is.
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_DROPSHADOW;
        windowClass.lpfnWndProc = &Impl::Proc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))  /* IDC_ARROW */;
        windowClass.lpszClassName = kClassName;
        if (RegisterClassExW(&windowClass) == 0) {
            ready.set_value(false);
            return;
        }

        // On the screen the mouse is on, so it opens where the user is looking.
        POINT cursor{};
        GetCursorPos(&cursor);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
        const RECT work = monitor.rcWork;
        const auto centered = [&work](int width, int height) {
            return POINT{ work.left + ((work.right - work.left) - width) / 2, work.top + ((work.bottom - work.top) - height) / 2 };
        };

        const POINT start = centered(EditorSplashPainter::kLogicalWidth, EditorSplashPainter::kLogicalHeight);
        window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, kWindowTitle, WS_POPUP, start.x, start.y,
            EditorSplashPainter::kLogicalWidth, EditorSplashPainter::kLogicalHeight, nullptr, nullptr, instance, this);
        if (window == nullptr) {
            UnregisterClassW(kClassName, instance);
            ready.set_value(false);
            return;
        }
        // The window now knows its monitor's DPI: size it for that and centre it again.
        dpi = static_cast<int>(GetDpiForWindow(window));
        const RECT size = EditorSplashPainter::Layout(dpi).client;
        const POINT origin = centered(size.right, size.bottom);
        SetWindowPos(window, nullptr, origin.x, origin.y, size.right, size.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
        const int corners = kRoundCorners;
        DwmSetWindowAttribute(window, kCornerPreference, &corners, sizeof(corners));
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        ready.set_value(true);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        UnregisterClassW(kClassName, instance);
    }
};

EditorSplashWindow::EditorSplashWindow()
    : impl_(std::make_unique<Impl>()) {
    impl_->snapshot.stage = EditorStartupProgress::StageText(EditorStartupStage::Project);
    std::promise<bool> ready;
    std::future<bool> opened = ready.get_future();
    impl_->thread = std::thread{ [impl = impl_.get(), promise = std::move(ready)]() mutable { impl->Run(std::move(promise)); } };
    if (!opened.get()) {
        impl_->thread.join();
        return;
    }
    EditorStartupProgress::SetSink([impl = impl_.get()](const EditorStartupSnapshot& snapshot) {
        {
            const std::lock_guard<std::mutex> lock{ impl->mutex };
            impl->snapshot = snapshot;
        }
        PostMessageW(impl->window, kStateChanged, 0, 0);
    });
}

EditorSplashWindow::~EditorSplashWindow() {
    Close();
}

void EditorSplashWindow::Close() {
    if (impl_ == nullptr) {
        return;
    }
    EditorStartupProgress::SetSink({});
    if (impl_->thread.joinable()) {
        PostMessageW(impl_->window, kCloseNow, 0, 0);
        impl_->thread.join();
    }
    impl_.reset();
}

} // namespace kb::editor

#endif
