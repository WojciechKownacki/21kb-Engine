#include "rendering/ProjectFilesOverlayRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserLayout.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "rendering/GdiDrawing.hpp"
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

void DrawDeleteConfirm(HDC dc, const RECT& content, const EditorTheme& theme, const EditorAssetBrowserState& state) {
    if (!state.IsDeleteConfirmOpen()) {
        return;
    }

    GdiDrawing::FillRectAlpha(dc, content, RGB(0, 0, 0), 88);
    const RECT dialog = EditorAssetBrowserGeometry::DeleteConfirmRect(content, state.DeleteConfirmOffsetX(), state.DeleteConfirmOffsetY());
    RECT shadow = dialog;
    OffsetRect(&shadow, 4, 5);
    GdiDrawing::FillRectAlpha(dc, shadow, RGB(0, 0, 0), 120);
    GdiDrawing::DrawSharpFrame(dc, dialog, Draw::Blend(Draw::Color(theme.panel), Draw::Color(theme.strip), 22), Draw::Color(theme.borderPanel));
    RECT topLine{ dialog.left, dialog.top, dialog.right, dialog.top + 2 };
    GdiDrawing::FillRectColor(dc, topLine, Draw::Color(theme.accent));

    RECT title{ dialog.left + 18, dialog.top + 16, dialog.right - 18, dialog.top + 40 };
    Draw::DrawTextWithFont(dc, title, "Delete folder?", Draw::Color(theme.textPrimary), 14, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const std::string folder = state.SelectedContentFolder().empty()
        ? kb::assets::NormalizeAssetPath(state.SelectedFolder())
        : kb::assets::NormalizeAssetPath(state.SelectedContentFolder());
    const std::string text = "This will remove " + folder + ".";
    RECT body{ dialog.left + 18, dialog.top + 50, dialog.right - 18, dialog.top + 86 };
    Draw::DrawTextWithFont(dc, body, text.c_str(), Draw::Color(theme.textSecondary), 12, FW_NORMAL, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    Draw::DrawTextButton(dc, EditorAssetBrowserGeometry::DeleteConfirmAcceptRect(dialog), theme, "Delete", true);
    Draw::DrawTextButton(dc, EditorAssetBrowserGeometry::DeleteConfirmCancelRect(dialog), theme, "Cancel", false);
}

} // namespace

void ProjectFilesOverlayRenderer::Paint(
    HDC dc,
    const RECT& content,
    const RECT& overlayBounds,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    DrawContextMenu(dc, content, theme, state, manager);
    DrawDeleteConfirm(dc, overlayBounds, theme, state);
}

} // namespace kb::editor

#endif
