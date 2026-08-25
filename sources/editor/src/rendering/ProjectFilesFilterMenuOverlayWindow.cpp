#include "rendering/ProjectFilesFilterMenuOverlayWindow.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserLayout.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/components/EditorDialogStyle.hpp"

#include <windowsx.h>

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;

constexpr wchar_t kFilterMenuOverlayClassName[] = L"KBEditorProjectFilesFilterMenuOverlay";
constexpr int kCheckboxSize = 16;

[[nodiscard]] bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

[[nodiscard]] int HoverIndexAt(const RECT& client, int x, int y) noexcept {
    const RECT first = EditorAssetBrowserLayout::ContextMenuItemRect(client, 0);
    const RECT second = EditorAssetBrowserLayout::ContextMenuItemRect(client, 1);
    if (x >= first.left && x < first.right && y >= first.top && y < first.bottom) {
        return 0;
    }
    if (x >= second.left && x < second.right && y >= second.top && y < second.bottom) {
        return 1;
    }
    return -1;
}

void DrawCheckbox(HDC dc, RECT rect, const EditorTheme& theme, bool checked) {
    EditorDialogStyle::PaintCheckbox(dc, rect, theme, checked);
}

void DrawItem(HDC dc, RECT rect, const EditorTheme& theme, const char* label, HeroIconKind icon, bool checked, bool hovered) {
    GdiDrawing::FillRectColor(
        dc,
        rect,
        EditorDialogStyle::Color(hovered ? theme.toolbarButton : theme.strip));

    const int checkboxTop = rect.top + ((rect.bottom - rect.top) - kCheckboxSize) / 2;
    RECT checkbox{ rect.left + 8, checkboxTop, rect.left + 8 + kCheckboxSize, checkboxTop + kCheckboxSize };
    DrawCheckbox(dc, checkbox, theme, checked);

    RECT iconRect{ rect.left + 29, rect.top + 5, rect.left + 45, rect.bottom - 5 };
    HeroIconPainter::Draw(dc, iconRect, icon, icon == HeroIconKind::Folder ? Draw::FolderColor(false) : RGB(78, 150, 244), 1);

    RECT text{ rect.left + 52, rect.top, rect.right - 8, rect.bottom };
    EditorDialogStyle::PaintText(
        dc,
        text,
        label,
        EditorDialogStyle::Color(checked || hovered ? theme.textPrimary : theme.textSecondary));
}

} // namespace

ProjectFilesFilterMenuOverlayWindow::~ProjectFilesFilterMenuOverlayWindow() {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        const HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
    }
}

void ProjectFilesFilterMenuOverlayWindow::Show(HWND parent, const RECT& assetContent, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    if (parent == nullptr || !sceneContext.AssetBrowser().IsFilterMenuOpen() || !EnsureWindow(parent)) {
        Hide();
        return;
    }

    parent_ = parent;
    theme_ = theme;
    sceneContext_ = &sceneContext;

    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(assetContent, sceneContext.AssetBrowser().TreeWidth());
    RECT menu = EditorAssetBrowserLayout::FilterMenuRect(layout);
    POINT screen{ menu.left, menu.top };
    ClientToScreen(parent, &screen);
    RECT nextBounds{
        screen.x,
        screen.y,
        screen.x + menu.right - menu.left,
        screen.y + menu.bottom - menu.top,
    };
    const bool movedOrResized = !SameRect(screenBounds_, nextBounds);
    const bool firstShow = !shown_;
    const bool filterValuesChanged = lastShowFolders_ != sceneContext.AssetBrowser().ShowFolders()
        || lastShowTemplates_ != sceneContext.AssetBrowser().ShowTemplates();
    if (movedOrResized || firstShow) {
        screenBounds_ = nextBounds;
        SetWindowPos(
            window_,
            HWND_TOP,
            screenBounds_.left,
            screenBounds_.top,
            screenBounds_.right - screenBounds_.left,
            screenBounds_.bottom - screenBounds_.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        shown_ = true;
    }
    if (movedOrResized || filterValuesChanged || firstShow) {
        lastShowFolders_ = sceneContext.AssetBrowser().ShowFolders();
        lastShowTemplates_ = sceneContext.AssetBrowser().ShowTemplates();
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void ProjectFilesFilterMenuOverlayWindow::Hide() noexcept {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        ShowWindow(window_, SW_HIDE);
    }
    shown_ = false;
    hoveredIndex_ = -1;
}

bool ProjectFilesFilterMenuOverlayWindow::EnsureWindow(HWND parent) {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &ProjectFilesFilterMenuOverlayWindow::WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kFilterMenuOverlayClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kFilterMenuOverlayClassName,
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

void ProjectFilesFilterMenuOverlayWindow::Paint(HDC dc) const {
    if (sceneContext_ == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(window_, &client);
    EditorDialogStyle::PaintSurface(dc, client, theme_);

    RECT first = EditorAssetBrowserLayout::ContextMenuItemRect(client, 0);
    RECT second = EditorAssetBrowserLayout::ContextMenuItemRect(client, 1);
    DrawItem(dc, first, theme_, "Folder", HeroIconKind::Folder, sceneContext_->AssetBrowser().ShowFolders(), hoveredIndex_ == 0);
    RECT separator{ client.left + 8, first.bottom + 3, client.right - 8, first.bottom + 4 };
    EditorDialogStyle::PaintDivider(dc, separator, theme_);
    DrawItem(dc, second, theme_, "Template", HeroIconKind::Cube, sceneContext_->AssetBrowser().ShowTemplates(), hoveredIndex_ == 1);
}

void ProjectFilesFilterMenuOverlayWindow::ForwardMouseMessage(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (parent_ == nullptr || IsWindow(parent_) == 0) {
        return;
    }
    POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    ClientToScreen(window_, &point);
    ScreenToClient(parent_, &point);
    SendMessageW(parent_, message, wparam, MAKELPARAM(point.x, point.y));
}

void ProjectFilesFilterMenuOverlayWindow::HandleMouseMove(int x, int y) {
    RECT client{};
    GetClientRect(window_, &client);
    const int hovered = HoverIndexAt(client, x, y);
    if (hovered == hoveredIndex_) {
        return;
    }
    hoveredIndex_ = hovered;
    InvalidateRect(window_, nullptr, FALSE);
}

LRESULT CALLBACK ProjectFilesFilterMenuOverlayWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* overlay = reinterpret_cast<ProjectFilesFilterMenuOverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
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
    case WM_MOUSEMOVE:
        if (overlay != nullptr) {
            overlay->HandleMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (overlay != nullptr) {
            overlay->ForwardMouseMessage(message, wparam, lparam);
            if (overlay->sceneContext_ != nullptr && !overlay->sceneContext_->AssetBrowser().IsFilterMenuOpen()) {
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
            overlay->hoveredIndex_ = -1;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
