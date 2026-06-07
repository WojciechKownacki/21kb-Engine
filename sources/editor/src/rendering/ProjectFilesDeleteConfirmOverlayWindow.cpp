#include "rendering/ProjectFilesDeleteConfirmOverlayWindow.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectFilesOverlayRenderer.hpp"

#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kDeleteConfirmOverlayClassName[] = L"KBEditorProjectFilesDeleteConfirmOverlay";

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

    const RECT nextBounds = ResolveScreenBounds();
    const bool movedOrResized = !SameRect(screenBounds_, nextBounds);
    const bool wasShown = shown_;
    const bool resized = (screenBounds_.right - screenBounds_.left) != (nextBounds.right - nextBounds.left)
        || (screenBounds_.bottom - screenBounds_.top) != (nextBounds.bottom - nextBounds.top);
    if (movedOrResized || !shown_) {
        static_cast<void>(MoveToCurrentBounds(true));
    }
    if (movedOrResized || resized || !wasShown) {
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
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
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

RECT ProjectFilesDeleteConfirmOverlayWindow::ResolveScreenBounds() const noexcept {
    if (parent_ == nullptr || sceneContext_ == nullptr || IsWindow(parent_) == 0) {
        return {};
    }

    RECT client{};
    GetClientRect(parent_, &client);
    const RECT dialog = EditorAssetBrowserGeometry::DeleteConfirmRect(
        client,
        sceneContext_->AssetBrowser().DeleteConfirmOffsetX(),
        sceneContext_->AssetBrowser().DeleteConfirmOffsetY());
    POINT screen{ dialog.left, dialog.top };
    ClientToScreen(parent_, &screen);
    return RECT{
        screen.x,
        screen.y,
        screen.x + dialog.right - dialog.left,
        screen.y + dialog.bottom - dialog.top,
    };
}

bool ProjectFilesDeleteConfirmOverlayWindow::MoveToCurrentBounds(bool showWindow) noexcept {
    if (window_ == nullptr || IsWindow(window_) == 0) {
        return false;
    }

    const RECT nextBounds = ResolveScreenBounds();
    if (SameRect(screenBounds_, nextBounds) && shown_) {
        return false;
    }

    screenBounds_ = nextBounds;
    SetWindowPos(
        window_,
        HWND_TOPMOST,
        screenBounds_.left,
        screenBounds_.top,
        screenBounds_.right - screenBounds_.left,
        screenBounds_.bottom - screenBounds_.top,
        SWP_NOACTIVATE | (showWindow ? SWP_SHOWWINDOW : 0U));
    shown_ = shown_ || showWindow;
    return true;
}

void ProjectFilesDeleteConfirmOverlayWindow::Paint(HDC dc) const {
    if (sceneContext_ == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(window_, &client);
    GdiDrawing::FillRectColor(dc, client, RGB(18, 20, 24));
    ProjectFilesOverlayRenderer::PaintDeleteConfirmDialogAt(
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

void ProjectFilesDeleteConfirmOverlayWindow::ForwardMouseWheel(WPARAM wparam, LPARAM lparam) const {
    if (parent_ == nullptr || IsWindow(parent_) == 0) {
        return;
    }
    SendMessageW(parent_, WM_MOUSEWHEEL, wparam, lparam);
}

ProjectFilesDeleteConfirmOverlayWindow::StateSnapshot ProjectFilesDeleteConfirmOverlayWindow::SnapshotState() const noexcept {
    if (sceneContext_ == nullptr) {
        return {};
    }
    const EditorAssetBrowserState& state = sceneContext_->AssetBrowser();
    return StateSnapshot{
        .open = state.IsDeleteConfirmOpen(),
        .offsetX = state.DeleteConfirmOffsetX(),
        .offsetY = state.DeleteConfirmOffsetY(),
        .listScroll = state.DeleteConfirmListScrollOffset(),
    };
}

bool ProjectFilesDeleteConfirmOverlayWindow::SameSnapshot(const StateSnapshot& left, const StateSnapshot& right) noexcept {
    return left.open == right.open
        && left.offsetX == right.offsetX
        && left.offsetY == right.offsetY
        && left.listScroll == right.listScroll;
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
            const bool pointerClick = message == WM_LBUTTONDOWN
                || message == WM_RBUTTONDOWN
                || message == WM_LBUTTONUP
                || message == WM_RBUTTONUP;
            const StateSnapshot before = overlay->SnapshotState();
            overlay->ForwardMouseMessage(message, wparam, lparam);
            const StateSnapshot after = overlay->SnapshotState();
            if (overlay->sceneContext_ != nullptr && !after.open) {
                ShowWindow(window, SW_HIDE);
            } else if (before.offsetX != after.offsetX || before.offsetY != after.offsetY) {
                static_cast<void>(overlay->MoveToCurrentBounds(false));
                if (before.listScroll != after.listScroll) {
                    InvalidateRect(window, nullptr, FALSE);
                }
            } else if (before.listScroll != after.listScroll || pointerClick) {
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (overlay != nullptr) {
            const StateSnapshot before = overlay->SnapshotState();
            overlay->ForwardMouseWheel(wparam, lparam);
            if (const StateSnapshot after = overlay->SnapshotState(); !SameSnapshot(before, after)) {
                InvalidateRect(window, nullptr, FALSE);
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
