#include "rendering/ProjectFilesOverlayRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserLayout.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"

#include <string>
#include <vector>

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;

void DrawContextMenu(HDC dc, const RECT& content, const EditorTheme& theme, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const std::vector<EditorAssetContextMenuItem> items = state.ContextMenuItems(manager);
    if (items.empty()) {
        return;
    }

    const RECT menu = EditorAssetBrowserLayout::ContextMenuRect(content, state.ContextMenuX(), state.ContextMenuY(), static_cast<int>(items.size()));
    RECT shadow = menu;
    OffsetRect(&shadow, 3, 3);
    GdiDrawing::FillRectAlpha(dc, shadow, RGB(0, 0, 0), 90);
    GdiDrawing::DrawSharpFrame(dc, menu, Draw::Color(theme.strip), Draw::Color(theme.borderPanel));

    for (std::size_t index = 0; index < items.size(); ++index) {
        const RECT row = EditorAssetBrowserLayout::ContextMenuItemRect(menu, static_cast<int>(index));
        const bool hovered = state.ContextMenuHoveredCommand() == items[index].command;
        if (hovered) {
            GdiDrawing::FillRectAlpha(dc, row, Draw::Color(theme.accent), 38);
        }
        Draw::DrawLabel(dc, Draw::Inset(row, 9, 0), items[index].label, hovered ? Draw::Color(theme.textPrimary) : Draw::Color(theme.textSecondary));
        if (index + 1 < items.size()) {
            RECT separator{ menu.left + 8, row.bottom + 3, menu.right - 8, row.bottom + 4 };
            Draw::DrawHairline(dc, separator, Draw::Color(theme.borderPanel));
        }
    }
}

void DrawDropActionMenu(HDC dc, const RECT& content, const EditorTheme& theme, const EditorAssetBrowserState& state) {
    if (!state.IsDropActionMenuOpen()) {
        return;
    }

    const RECT menu = EditorAssetBrowserLayout::ContextMenuRect(content, state.DropActionMenuX(), state.DropActionMenuY(), 2);
    RECT shadow = menu;
    OffsetRect(&shadow, 3, 3);
    GdiDrawing::FillRectAlpha(dc, shadow, RGB(0, 0, 0), 90);
    GdiDrawing::DrawSharpFrame(dc, menu, Draw::Color(theme.strip), Draw::Blend(Draw::Color(theme.borderPanel), Draw::Color(theme.accent), 20));

    constexpr const char* labels[2] = { "Move Here", "Copy Here" };
    constexpr EditorAssetDropAction commands[2] = { EditorAssetDropAction::MoveHere, EditorAssetDropAction::CopyHere };
    for (int index = 0; index < 2; ++index) {
        const RECT row = EditorAssetBrowserLayout::ContextMenuItemRect(menu, index);
        const bool hovered = state.DropActionHoveredCommand() == commands[index];
        if (hovered) {
            GdiDrawing::FillRectAlpha(dc, row, Draw::Color(theme.accent), 38);
        }
        Draw::DrawLabel(dc, Draw::Inset(row, 9, 0), labels[index], hovered ? Draw::Color(theme.textPrimary) : Draw::Color(theme.textSecondary));
        if (index == 0) {
            RECT separator{ menu.left + 8, row.bottom + 3, menu.right - 8, row.bottom + 4 };
            Draw::DrawHairline(dc, separator, Draw::Color(theme.borderPanel));
        }
    }
}

void DrawModalButton(HDC dc, RECT rect, const EditorTheme& theme, const char* text, bool destructive) {
    const COLORREF base = destructive
        ? RGB(77, 48, 26)
        : Draw::Blend(Draw::Color(theme.panel), Draw::Color(theme.strip), 28);
    const COLORREF border = destructive
        ? Draw::Blend(Draw::Color(theme.accent), RGB(255, 184, 48), 32)
        : Draw::Blend(Draw::Color(theme.borderPanel), Draw::Color(theme.textSecondary), 20);
    GdiDrawing::DrawSharpFrame(dc, rect, base, border);
    RECT top{ rect.left + 1, rect.top + 1, rect.right - 1, rect.top + 2 };
    GdiDrawing::FillRectColor(dc, top, destructive ? RGB(214, 152, 31) : Draw::Blend(Draw::Color(theme.textSecondary), Draw::Color(theme.borderPanel), 55));
    Draw::DrawTextWithFont(dc, Draw::Inset(rect, 8, 0), text, destructive ? RGB(255, 241, 210) : Draw::Color(theme.textPrimary), 12, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawDeleteConfirmDialog(HDC dc, const RECT& bounds, const EditorTheme& theme, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager, bool shadowed) {
    const RECT dialog = EditorAssetBrowserGeometry::DeleteConfirmRect(bounds, state.DeleteConfirmOffsetX(), state.DeleteConfirmOffsetY());
    if (shadowed) {
        RECT shadow = dialog;
        OffsetRect(&shadow, 6, 8);
        GdiDrawing::FillRectAlpha(dc, shadow, RGB(0, 0, 0), 155);
    }
    GdiDrawing::DrawSharpFrame(dc, dialog, RGB(29, 31, 35), Draw::Blend(Draw::Color(theme.borderPanel), Draw::Color(theme.textSecondary), 22));
    RECT topLine{ dialog.left, dialog.top, dialog.right, dialog.top + 3 };
    GdiDrawing::FillRectColor(dc, topLine, RGB(226, 171, 34));
    RECT header{ dialog.left + 1, dialog.top + 3, dialog.right - 1, dialog.top + 62 };
    GdiDrawing::FillRectColor(dc, header, RGB(25, 27, 31));

    const bool deletingAsset = state.SelectionKind() == EditorAssetBrowserSelectionKind::Asset;
    RECT icon{ dialog.left + 22, dialog.top + 22, dialog.left + 46, dialog.top + 46 };
    HeroIconPainter::Draw(dc, icon, deletingAsset ? HeroIconKind::Cube : HeroIconKind::Folder, deletingAsset ? RGB(104, 166, 245) : RGB(232, 181, 56), 2);
    RECT title{ dialog.left + 58, dialog.top + 18, dialog.right - 24, dialog.top + 42 };
    Draw::DrawTextWithFont(dc, title, deletingAsset ? "Delete asset?" : "Delete folder?", Draw::Color(theme.textPrimary), 14, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    std::string target;
    if (deletingAsset) {
        if (const kb::assets::AssetMetadata* metadata = state.SelectedMetadata(manager)) {
            target = metadata->name;
        }
    } else {
        target = state.SelectedContentFolder().empty()
            ? kb::assets::NormalizeAssetPath(state.SelectedFolder())
            : kb::assets::NormalizeAssetPath(state.SelectedContentFolder());
    }
    if (target.empty()) {
        target = deletingAsset ? "the selected asset" : "the selected folder";
    }
    RECT body{ dialog.left + 24, dialog.top + 78, dialog.right - 24, dialog.top + 100 };
    Draw::DrawTextWithFont(dc, body, "This action will permanently remove the selected item from Project Files.", Draw::Color(theme.textSecondary), 12, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT targetBox{ dialog.left + 24, dialog.top + 108, dialog.right - 24, dialog.top + 136 };
    GdiDrawing::DrawSharpFrame(dc, targetBox, RGB(22, 24, 28), Draw::Blend(Draw::Color(theme.borderPanel), Draw::Color(theme.textSecondary), 18));
    Draw::DrawTextWithFont(dc, Draw::Inset(targetBox, 10, 0), target.c_str(), Draw::Color(theme.textPrimary), 12, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DrawModalButton(dc, EditorAssetBrowserGeometry::DeleteConfirmAcceptRect(dialog), theme, "Delete", true);
    DrawModalButton(dc, EditorAssetBrowserGeometry::DeleteConfirmCancelRect(dialog), theme, "Cancel", false);
}

void DrawDeleteConfirm(HDC dc, const RECT& content, const EditorTheme& theme, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    if (!state.IsDeleteConfirmOpen()) {
        return;
    }

    GdiDrawing::FillRectAlpha(dc, content, RGB(0, 0, 0), 116);
    DrawDeleteConfirmDialog(dc, content, theme, state, manager, true);
}

} // namespace

void ProjectFilesOverlayRenderer::Paint(
    HDC dc,
    const RECT& content,
    const RECT& overlayBounds,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    (void)overlayBounds;
    DrawContextMenu(dc, content, theme, state, manager);
    DrawDropActionMenu(dc, content, theme, state);
}

void ProjectFilesOverlayRenderer::PaintDeleteConfirm(
    HDC dc,
    const RECT& bounds,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    DrawDeleteConfirm(dc, bounds, theme, state, manager);
}

void ProjectFilesOverlayRenderer::PaintDeleteConfirmDialogOnly(
    HDC dc,
    const RECT& bounds,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    if (!state.IsDeleteConfirmOpen()) {
        return;
    }

    DrawDeleteConfirmDialog(dc, bounds, theme, state, manager, false);
}

} // namespace kb::editor

#endif
