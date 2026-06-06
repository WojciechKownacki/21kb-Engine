#include "rendering/ProjectFilesDeleteConfirmOverlayWindow.hpp"

#if defined(_WIN32)
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectFilesOverlayRenderer.hpp"

#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kDeleteConfirmOverlayClassName[] = L"KBEditorProjectFilesDeleteConfirmOverlay";
constexpr COLORREF kTransparentColor = RGB(255, 0, 255);

[[nodiscard]] bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

} // namespace

ProjectFilesDeleteConfirmOverlayWindow::~ProjectFilesDeleteConfirmOverlayWindow() {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        const HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
    }
}

void ProjectFilesDeleteConfirmOverlayWindow::Show(HWND parent, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    if (parent == nullptr || !sceneContext.AssetBrowser().IsDeleteConfirmOpen() || !EnsureWindow(parent)) {
        Hide();
        return;
    }

    parent_ = parent;
    theme_ = theme;
    sceneContext_ = &sceneContext;

    RECT client{};
    GetClientRect(parent, &client);
    POINT screen{ client.left, client.top };
    ClientToScreen(parent, &screen);
    RECT nextBounds{
        screen.x,
        screen.y,
        screen.x + client.right - client.left,
        screen.y + client.bottom - client.top,
    };
    const bool movedOrResized = !SameRect(screenBounds_, nextBounds);
    const bool resized = (screenBounds_.right - screenBounds_.left) != (nextBounds.right - nextBounds.left)
        || (screenBounds_.bottom - screenBounds_.top) != (nextBounds.bottom - nextBounds.top);
    if (movedOrResized || !shown_) {
        screenBounds_ = nextBounds;
        SetWindowPos(
            window_,
            HWND_TOP,
            screenBounds_.left,
            screenBounds_.top,
            screenBounds_.right - screenBounds_.left,
            screenBounds_.bottom - screenBounds_.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW | (movedOrResized ? 0 : SWP_NOMOVE | SWP_NOSIZE));
        shown_ = true;
    }
    SetLayeredWindowAttributes(window_, kTransparentColor, 0, LWA_COLORKEY);
    if (!movedOrResized || resized || !shown_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void ProjectFilesDeleteConfirmOverlayWindow::Hide() noexcept {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        ShowWindow(window_, SW_HIDE);
    }
    shown_ = false;
}

bool ProjectFilesDeleteConfirmOverlayWindow::EnsureWindow(HWND parent) {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &ProjectFilesDeleteConfirmOverlayWindow::WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kDeleteConfirmOverlayClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kDeleteConfirmOverlayClassName,
        L"",
        WS_POPUP,
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

void ProjectFilesDeleteConfirmOverlayWindow::Paint(HDC dc) const {
    if (sceneContext_ == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(window_, &client);
    GdiDrawing::FillRectColor(dc, client, kTransparentColor);
    ProjectFilesOverlayRenderer::PaintDeleteConfirmDialogOnly(
        dc,
        client,
        theme_,
        sceneContext_->AssetBrowser(),
        sceneContext_->Scene().Assets().Manager());
}

void ProjectFilesDeleteConfirmOverlayWindow::ForwardMouseMessage(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (parent_ == nullptr || IsWindow(parent_) == 0) {
        return;
    }
    POINT point{
        GET_X_LPARAM(lparam),
        GET_Y_LPARAM(lparam),
    };
    ClientToScreen(window_, &point);
    ScreenToClient(parent_, &point);
    SendMessageW(parent_, message, wparam, MAKELPARAM(point.x, point.y));
}

LRESULT CALLBACK ProjectFilesDeleteConfirmOverlayWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* overlay = reinterpret_cast<ProjectFilesDeleteConfirmOverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
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
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
        if (overlay != nullptr) {
            overlay->ForwardMouseMessage(message, wparam, lparam);
            if (overlay->sceneContext_ != nullptr && !overlay->sceneContext_->AssetBrowser().IsDeleteConfirmOpen()) {
                ShowWindow(window, SW_HIDE);
            }
            return 0;
        }
        break;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
        return TRUE;
    case WM_NCDESTROY:
        if (overlay != nullptr && overlay->window_ == window) {
            overlay->window_ = nullptr;
            overlay->parent_ = nullptr;
            overlay->sceneContext_ = nullptr;
            overlay->shown_ = false;
            overlay->screenBounds_ = RECT{};
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
