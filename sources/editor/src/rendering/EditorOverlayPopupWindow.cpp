#include "rendering/EditorOverlayPopupWindow.hpp"

#if defined(_WIN32)
#include "rendering/GdiBackBufferRenderer.hpp"

#include <windowsx.h>

namespace kb::editor {
namespace {

[[nodiscard]] bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

[[nodiscard]] bool IsClientInput(UINT message) noexcept {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSELEAVE:
    case WM_MOUSEWHEEL:
    case WM_CAPTURECHANGED:
        return true;
    default:
        return false;
    }
}

} // namespace

EditorOverlayPopupWindow::EditorOverlayPopupWindow(const wchar_t* className, Client& client) noexcept
    : className_(className)
    , client_(&client) {}

EditorOverlayPopupWindow::~EditorOverlayPopupWindow() {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        const HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
    }
}

bool EditorOverlayPopupWindow::Ensure(HWND owner) {
    if (owner == nullptr) {
        return false;
    }
    if (window_ != nullptr && IsWindow(window_) != 0) {
        if (owner_ != owner) {
            static_cast<void>(SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner)));
            owner_ = owner;
        }
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &EditorOverlayPopupWindow::WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = className_;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        className_,
        L"",
        WS_POPUP,
        0,
        0,
        1,
        1,
        owner,
        nullptr,
        windowClass.hInstance,
        this);
    owner_ = window_ != nullptr ? owner : nullptr;
    shown_ = false;
    screenBounds_ = RECT{};
    return window_ != nullptr;
}

bool EditorOverlayPopupWindow::ShowAt(const RECT& screenBounds, HWND insertAfter) noexcept {
    if (window_ == nullptr || IsWindow(window_) == 0) {
        return false;
    }
    if (shown_ && SameRect(screenBounds_, screenBounds)) {
        return false;
    }
    screenBounds_ = screenBounds;
    SetWindowPos(
        window_,
        insertAfter,
        screenBounds_.left,
        screenBounds_.top,
        screenBounds_.right - screenBounds_.left,
        screenBounds_.bottom - screenBounds_.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    shown_ = true;
    return true;
}

void EditorOverlayPopupWindow::Hide() noexcept {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        if (GetCapture() == window_) {
            ReleaseCapture();
        }
        ShowWindow(window_, SW_HIDE);
    }
    shown_ = false;
}

void EditorOverlayPopupWindow::Invalidate() const noexcept {
    if (window_ != nullptr) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void EditorOverlayPopupWindow::ForwardToOwner(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (window_ == nullptr || owner_ == nullptr || IsWindow(owner_) == 0) {
        return;
    }
    POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    ClientToScreen(window_, &point);
    ScreenToClient(owner_, &point);
    SendMessageW(owner_, message, wparam, MAKELPARAM(point.x, point.y));
}

RECT EditorOverlayPopupWindow::OwnerClientToScreen(HWND owner, const RECT& rect) noexcept {
    POINT origin{ rect.left, rect.top };
    if (owner != nullptr) {
        ClientToScreen(owner, &origin);
    }
    return RECT{
        origin.x,
        origin.y,
        origin.x + (rect.right - rect.left),
        origin.y + (rect.bottom - rect.top),
    };
}

HWND EditorOverlayPopupWindow::Window() const noexcept {
    return window_;
}

HWND EditorOverlayPopupWindow::Owner() const noexcept {
    return owner_;
}

bool EditorOverlayPopupWindow::Shown() const noexcept {
    return shown_;
}

RECT EditorOverlayPopupWindow::ClientBounds() const noexcept {
    RECT client{};
    if (window_ != nullptr) {
        GetClientRect(window_, &client);
    }
    return client;
}

LRESULT CALLBACK EditorOverlayPopupWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* popup = reinterpret_cast<EditorOverlayPopupWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        GdiBackBufferRenderer::Paint(
            window,
            [](const GdiBackBufferPaintContext& paint, void* context) {
                auto* target = static_cast<EditorOverlayPopupWindow*>(context);
                if (target != nullptr && target->client_ != nullptr) {
                    target->client_->PaintOverlay(paint.dc, paint.client);
                }
            },
            popup);
        return 0;
    case WM_PRINTCLIENT:
        if (popup != nullptr && popup->client_ != nullptr) {
            RECT client{};
            GetClientRect(window, &client);
            popup->client_->PaintOverlay(reinterpret_cast<HDC>(wparam), client);
        }
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
        return TRUE;
    case WM_NCDESTROY:
        if (popup != nullptr && popup->window_ == window) {
            popup->window_ = nullptr;
            popup->owner_ = nullptr;
            popup->shown_ = false;
            popup->screenBounds_ = RECT{};
        }
        break;
    default:
        if (popup != nullptr && popup->client_ != nullptr && IsClientInput(message) &&
            popup->client_->HandleOverlayMessage(message, wparam, lparam)) {
            return 0;
        }
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
