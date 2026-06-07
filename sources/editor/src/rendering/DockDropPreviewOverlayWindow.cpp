#include "rendering/DockDropPreviewOverlayWindow.hpp"

#if defined(_WIN32)
#include "rendering/DockDropPreviewRenderer.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {
namespace {

constexpr wchar_t kDockPreviewOverlayClassName[] = L"KBEditorDockDropPreviewOverlay";

[[nodiscard]] RECT LocalRect(const DockDropPreview& preview) noexcept {
    return RECT{
        .left = 0,
        .top = 0,
        .right = preview.rect.width,
        .bottom = preview.rect.height,
    };
}

[[nodiscard]] DockDropPreview LocalPreview(const DockDropPreview& preview) noexcept {
    DockDropPreview local = preview;
    local.rect.x = 0;
    local.rect.y = 0;
    return local;
}

[[nodiscard]] BYTE OverlayAlpha(DockDropPreviewKind kind) noexcept {
    return kind == DockDropPreviewKind::StripMarker ? 230U : 190U;
}

[[nodiscard]] POINT ClientPointToScreen(HWND parent, const DockRect& rect) noexcept {
    POINT point{ .x = rect.x, .y = rect.y };
    ClientToScreen(parent, &point);
    return point;
}

} // namespace

DockDropPreviewOverlayWindow::~DockDropPreviewOverlayWindow() {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        const HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
    }
}

void DockDropPreviewOverlayWindow::Show(HWND parent, const DockDropPreview& preview, const EditorTheme& theme) {
    if (parent == nullptr || preview.rect.Empty() || !EnsureWindow(parent)) {
        Hide();
        return;
    }

    preview_ = preview;
    theme_ = theme;
    const POINT screen = ClientPointToScreen(parent, preview.rect);
    SetLayeredWindowAttributes(window_, 0, OverlayAlpha(preview.kind), LWA_ALPHA);
    SetWindowPos(
        window_,
        HWND_TOPMOST,
        screen.x,
        screen.y,
        preview.rect.width,
        preview.rect.height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, FALSE);
}

void DockDropPreviewOverlayWindow::Hide() noexcept {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        ShowWindow(window_, SW_HIDE);
    }
}

bool DockDropPreviewOverlayWindow::EnsureWindow(HWND parent) {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &DockDropPreviewOverlayWindow::WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kDockPreviewOverlayClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        kDockPreviewOverlayClassName,
        L"",
        WS_POPUP | WS_DISABLED,
        0,
        0,
        1,
        1,
        parent,
        nullptr,
        windowClass.hInstance,
        this);
    return window_ != nullptr;
}

void DockDropPreviewOverlayWindow::Paint(HDC dc) const {
    const RECT localRect = LocalRect(preview_);
    GdiDrawing::FillRectColor(dc, localRect, RGB(13, 17, 23));
    DockDropPreviewRenderer{}.Paint(dc, LocalPreview(preview_), theme_);
}

LRESULT CALLBACK DockDropPreviewOverlayWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* overlay = reinterpret_cast<DockDropPreviewOverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (overlay != nullptr) {
            overlay->Paint(dc);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        if (overlay != nullptr && overlay->window_ == window) {
            overlay->window_ = nullptr;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
