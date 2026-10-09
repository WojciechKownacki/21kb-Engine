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

ProjectFilesFilterMenuOverlayWindow::ProjectFilesFilterMenuOverlayWindow() noexcept
    : popup_(kFilterMenuOverlayClassName, *this) {}

void ProjectFilesFilterMenuOverlayWindow::Show(HWND parent, const RECT& assetContent, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    if (parent == nullptr || !sceneContext.AssetBrowser().IsFilterMenuOpen() || !popup_.Ensure(parent)) {
        Hide();
        return;
    }

    theme_ = theme;
    sceneContext_ = &sceneContext;

    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(assetContent, sceneContext.AssetBrowser().TreeWidth());
    const RECT nextBounds = EditorOverlayPopupWindow::OwnerClientToScreen(parent, EditorAssetBrowserLayout::FilterMenuRect(layout));
    const bool placed = popup_.ShowAt(nextBounds);
    const bool filterValuesChanged = lastShowFolders_ != sceneContext.AssetBrowser().ShowFolders()
        || lastShowTemplates_ != sceneContext.AssetBrowser().ShowTemplates();
    if (placed || filterValuesChanged) {
        lastShowFolders_ = sceneContext.AssetBrowser().ShowFolders();
        lastShowTemplates_ = sceneContext.AssetBrowser().ShowTemplates();
        popup_.Invalidate();
    }
}

void ProjectFilesFilterMenuOverlayWindow::Hide() noexcept {
    popup_.Hide();
    hoveredIndex_ = -1;
}

void ProjectFilesFilterMenuOverlayWindow::PaintOverlay(HDC dc, const RECT& client) {
    if (sceneContext_ == nullptr) {
        return;
    }

    EditorDialogStyle::PaintSurface(dc, client, theme_);

    RECT first = EditorAssetBrowserLayout::ContextMenuItemRect(client, 0);
    RECT second = EditorAssetBrowserLayout::ContextMenuItemRect(client, 1);
    DrawItem(dc, first, theme_, "Folder", HeroIconKind::Folder, sceneContext_->AssetBrowser().ShowFolders(), hoveredIndex_ == 0);
    RECT separator{ client.left + 8, first.bottom + 3, client.right - 8, first.bottom + 4 };
    EditorDialogStyle::PaintDivider(dc, separator, theme_);
    DrawItem(dc, second, theme_, "Template", HeroIconKind::Cube, sceneContext_->AssetBrowser().ShowTemplates(), hoveredIndex_ == 1);
}

void ProjectFilesFilterMenuOverlayWindow::HandleMouseMove(int x, int y) {
    const int hovered = HoverIndexAt(popup_.ClientBounds(), x, y);
    if (hovered == hoveredIndex_) {
        return;
    }
    hoveredIndex_ = hovered;
    popup_.Invalidate();
}

bool ProjectFilesFilterMenuOverlayWindow::HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_MOUSEMOVE:
        HandleMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return true;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        popup_.ForwardToOwner(message, wparam, lparam);
        if (sceneContext_ != nullptr && !sceneContext_->AssetBrowser().IsFilterMenuOpen()) {
            Hide();
        }
        return true;
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
