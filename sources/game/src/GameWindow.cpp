#include "GameWindow.hpp"

#include "engine/platform/win32/Win32InputCollector.hpp"

namespace kb::game {

GameWindow::~GameWindow() {
    if (window_ != nullptr) {
        static_cast<void>(UnregisterTouchWindow(window_));
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (windowClass_ != 0U) {
        UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
        windowClass_ = 0U;
    }
}

bool GameWindow::Open(
    const std::wstring& title,
    std::uint32_t width,
    std::uint32_t height,
    kb::input::Win32InputCollector& inputCollector) {
    if (width == 0U || height == 0U) {
        return false;
    }
    inputCollector_ = &inputCollector;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kWindowClassName;
    windowClass_ = RegisterClassExW(&windowClass);
    if (windowClass_ == 0U) {
        return false;
    }

    RECT frame{
        .left = 0,
        .top = 0,
        .right = static_cast<LONG>(width),
        .bottom = static_cast<LONG>(height),
    };
    if (AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE) == 0) {
        return false;
    }
    window_ = CreateWindowExW(
        0U,
        kWindowClassName,
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        frame.right - frame.left,
        frame.bottom - frame.top,
        nullptr,
        nullptr,
        windowClass.hInstance,
        this);
    if (window_ == nullptr) {
        return false;
    }
    if (RegisterTouchWindow(window_, 0U) == 0) {
        return false;
    }

    // Windows overrides the first ShowWindow of a process whose launcher passed
    // STARTF_USESHOWWINDOW, so a shortcut set to "Run: minimized" - or any
    // launcher that starts the game minimized - lands here with a zero client
    // area, which no renderer can be initialized against. The game asks for its
    // window back rather than refusing to start.
    ShowWindow(window_, SW_SHOWNORMAL);
    RECT client{};
    if (GetClientRect(window_, &client) == 0) {
        return false;
    }
    if (client.right <= client.left || client.bottom <= client.top) {
        ShowWindow(window_, SW_RESTORE);
        if (GetClientRect(window_, &client) == 0) {
            return false;
        }
    }
    if (client.right <= client.left || client.bottom <= client.top) {
        return false;
    }
    width_ = static_cast<std::uint32_t>(client.right - client.left);
    height_ = static_cast<std::uint32_t>(client.bottom - client.top);
    resizePending_ = true;

    UpdateWindow(window_);
    return true;
}

std::uint32_t GameWindow::Width() const noexcept {
    return width_;
}

std::uint32_t GameWindow::Height() const noexcept {
    return height_;
}

void* GameWindow::NativeWindowHandle() const noexcept {
    return window_;
}

void* GameWindow::NativeDisplayHandle() const noexcept {
    return nullptr;
}

bool GameWindow::PumpMessages() noexcept {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != 0) {
        if (message.message == WM_QUIT) {
            closeRequested_ = true;
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return !closeRequested_;
}

bool GameWindow::ConsumeResize(std::uint32_t& width, std::uint32_t& height) noexcept {
    if (!resizePending_) {
        return false;
    }
    resizePending_ = false;
    width = width_;
    height = height_;
    return true;
}

LRESULT CALLBACK GameWindow::WindowProc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    GameWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<GameWindow*>(create->lpCreateParams);
        if (self != nullptr) {
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    } else {
        self = reinterpret_cast<GameWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (self != nullptr) {
        switch (message) {
        case WM_SIZE: {
            const auto width = static_cast<std::uint32_t>(LOWORD(lparam));
            const auto height = static_cast<std::uint32_t>(HIWORD(lparam));
            if (width != self->width_ || height != self->height_) {
                self->width_ = width;
                self->height_ = height;
                self->resizePending_ = true;
            }
            break;
        }
        case WM_CLOSE:
            self->closeRequested_ = true;
            return 0;
        case WM_DESTROY:
            self->closeRequested_ = true;
            PostQuitMessage(0);
            break;
        default:
            break;
        }
        if (self->inputCollector_ != nullptr &&
            self->inputCollector_->HandleWindowMessage(window, message, wparam, lparam)) {
            return 0;
        }
    }

    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::game
