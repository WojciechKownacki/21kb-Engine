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
#include "rendering/components/EditorDialogStyle.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;

constexpr int kSummaryHeaderHeight = 26;
constexpr int kSummaryRowHeight = 24;
constexpr int kSummaryScrollbarWidth = 12;
constexpr int kLightingSubmenuWidth = 190;
constexpr int kLightingSubmenuRows = 4;
constexpr int kLightingSubmenuGap = 4;

[[nodiscard]] int Width(const RECT& rect) noexcept {
    return static_cast<int>(rect.right - rect.left);
}

[[nodiscard]] int Height(const RECT& rect) noexcept {
    return static_cast<int>(rect.bottom - rect.top);
}

[[nodiscard]] RECT CenteredRect(RECT outer, int left, int width, int height) noexcept {
    const int top = outer.top + std::max(0, (Height(outer) - height) / 2);
    return RECT{ left, top, left + width, top + height };
}

void DrawCheckbox(HDC dc, RECT row, const EditorTheme& theme, bool checked) {
    constexpr int checkboxColumn = 32;
    RECT box = CenteredRect(row, row.left + (checkboxColumn - 15) / 2, 15, 15);
    EditorDialogStyle::PaintCheckbox(dc, box, theme, checked);
}

[[nodiscard]] RECT SummaryViewportRect(const RECT& summary) noexcept {
    return RECT{
        summary.left + 1,
        summary.top + kSummaryHeaderHeight + 2,
        summary.right - 1,
        summary.bottom - 1,
    };
}

[[nodiscard]] bool IsLightingSubmenuCommand(EditorAssetContextCommand command) noexcept {
    return command == EditorAssetContextCommand::AddLighting
        || command == EditorAssetContextCommand::AddDirectionalLight
        || command == EditorAssetContextCommand::AddPointLight
        || command == EditorAssetContextCommand::AddSpotLight;
}

[[nodiscard]] bool HasLightingSubmenu(const std::vector<EditorAssetContextMenuItem>& items) noexcept {
    return std::ranges::any_of(items, [](const EditorAssetContextMenuItem& item) {
        return item.command == EditorAssetContextCommand::AddLighting;
    });
}

[[nodiscard]] RECT ContextMenuRectForItems(const RECT& content, int x, int y, const std::vector<EditorAssetContextMenuItem>& items) noexcept {
    (void)items;
    return EditorAssetBrowserLayout::ContextMenuRect(content, x, y, static_cast<int>(items.size()));
}

void DrawSelectionSummaryScrollbar(HDC dc, RECT viewport, const EditorTheme& theme, int rowCount, int scroll) {
    const int contentHeight = rowCount * kSummaryRowHeight;
    const int viewportHeight = Height(viewport);
    if (contentHeight <= viewportHeight || viewportHeight <= 0) {
        return;
    }

    RECT track{ viewport.right - kSummaryScrollbarWidth, viewport.top, viewport.right, viewport.bottom };
    const int thumbHeight = std::max(18, viewportHeight * viewportHeight / std::max(1, contentHeight));
    const int travel = std::max(1, viewportHeight - thumbHeight);
    const int maxScroll = std::max(1, contentHeight - viewportHeight);
    const int thumbTop = track.top + (scroll * travel) / maxScroll;
    RECT thumb{ track.left + 2, thumbTop + 2, track.right - 2, thumbTop + thumbHeight - 2 };
    EditorDialogStyle::PaintScrollbar(dc, track, thumb, theme);
}

void DrawSelectionSummary(
    HDC dc,
    RECT summary,
    const EditorTheme& theme,
    const std::vector<EditorAssetSelectionSummaryRow>& rows,
    int scroll,
    int maxScroll) {
    if (rows.empty()) {
        return;
    }
    if (Width(summary) <= 0 || Height(summary) <= 0) {
        return;
    }

    EditorDialogStyle::PaintListFrame(dc, summary, theme);
    RECT header{ summary.left + 1, summary.top + 1, summary.right - 1, summary.top + 1 + kSummaryHeaderHeight };
    GdiDrawing::FillRectColor(dc, header, EditorDialogStyle::Color(theme.strip));
    const int checkboxColumn = 32;
    const int idColumn = 86;
    const int typeColumn = 98;
    RECT idHeader{ header.left + checkboxColumn, header.top, header.left + checkboxColumn + idColumn, header.bottom };
    RECT nameHeader{ idHeader.right, header.top, header.right - typeColumn - kSummaryScrollbarWidth, header.bottom };
    RECT typeHeader{ nameHeader.right, header.top, header.right - kSummaryScrollbarWidth, header.bottom };
    Draw::DrawTextWithFont(dc, idHeader, "ID", Draw::Color(theme.textSecondary), 12, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    Draw::DrawTextWithFont(dc, nameHeader, "Name", Draw::Color(theme.textSecondary), 12, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    Draw::DrawTextWithFont(dc, typeHeader, "Object Type", Draw::Color(theme.textSecondary), 12, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    Draw::DrawHairline(dc, RECT{ summary.left + 1, header.bottom, summary.right - 1, header.bottom + 1 }, Draw::Color(theme.borderPanel));

    const RECT viewport = SummaryViewportRect(summary);
    scroll = std::clamp(scroll, 0, maxScroll);
    const int firstRow = std::max(0, scroll / kSummaryRowHeight);
    const int yOffset = scroll % kSummaryRowHeight;
    const int visibleRows = (std::max(0, Height(viewport)) / kSummaryRowHeight) + 2;
    const int lastRow = std::clamp(firstRow + visibleRows, 0, static_cast<int>(rows.size()));

    SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right - kSummaryScrollbarWidth, viewport.bottom);
    for (int index = firstRow; index < lastRow; ++index) {
        RECT row{
            viewport.left,
            viewport.top + (index - firstRow) * kSummaryRowHeight - yOffset,
            viewport.right - kSummaryScrollbarWidth,
            viewport.top + (index - firstRow + 1) * kSummaryRowHeight - yOffset,
        };
        if ((index % 2) == 0) {
            GdiDrawing::FillRectColor(dc, row, EditorDialogStyle::Color(theme.strip));
        }
        RECT id{ row.left + checkboxColumn, row.top, row.left + checkboxColumn + idColumn, row.bottom };
        RECT name{ id.right, row.top, row.right - typeColumn, row.bottom };
        RECT type{ name.right, row.top, row.right - 4, row.bottom };
        const EditorAssetSelectionSummaryRow& item = rows[static_cast<std::size_t>(index)];
        DrawCheckbox(dc, row, theme, item.checked);
        Draw::DrawTextWithFont(dc, id, item.id.c_str(), Draw::Color(theme.textDisabled), 12, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        Draw::DrawTextWithFont(dc, name, item.name.c_str(), Draw::Color(theme.textPrimary), 12, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        Draw::DrawTextWithFont(dc, type, item.objectType.c_str(), Draw::Color(theme.textSecondary), 12, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        Draw::DrawHairline(dc, RECT{ row.left, row.bottom - 1, row.right, row.bottom }, Draw::Color(theme.borderPanel));
    }
    RestoreDC(dc, -1);
    DrawSelectionSummaryScrollbar(dc, viewport, theme, static_cast<int>(rows.size()), scroll);
}

void DrawContextMenu(HDC dc, const RECT& content, const EditorTheme& theme, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const std::vector<EditorAssetContextMenuItem> items = state.ContextMenuItems(manager);
    if (items.empty()) {
        return;
    }

    const RECT menu = ContextMenuRectForItems(content, state.ContextMenuX(), state.ContextMenuY(), items);
    RECT shadow = menu;
    OffsetRect(&shadow, 3, 3);
    GdiDrawing::FillRectAlpha(dc, shadow, RGB(0, 0, 0), 90);
    EditorDialogStyle::PaintSurface(dc, menu, theme);

    for (std::size_t index = 0; index < items.size(); ++index) {
        const RECT row = EditorAssetBrowserLayout::ContextMenuItemRect(menu, static_cast<int>(index));
        const bool hovered = state.ContextMenuHoveredCommand() == items[index].command
            || (items[index].command == EditorAssetContextCommand::AddLighting && IsLightingSubmenuCommand(state.ContextMenuHoveredCommand()));
        EditorDialogStyle::PaintMenuRow(dc, row, theme, items[index].label, HeroIconKind::Cube, hovered, true, false);
        if (items[index].command == EditorAssetContextCommand::AddLighting) {
            Draw::DrawLabel(dc, RECT{ row.right - 20, row.top, row.right - 6, row.bottom }, ">", hovered ? Draw::Color(theme.textPrimary) : Draw::Color(theme.textSecondary));
        }
        if (index + 1 < items.size()) {
            RECT separator{ menu.left + 8, row.bottom + 3, menu.right - 8, row.bottom + 4 };
            EditorDialogStyle::PaintDivider(dc, separator, theme);
        }
    }

    const EditorAssetContextCommand hoveredCommand = state.ContextMenuHoveredCommand();
    const bool showLightingSubmenu = hoveredCommand == EditorAssetContextCommand::AddLighting
        || hoveredCommand == EditorAssetContextCommand::AddDirectionalLight
        || hoveredCommand == EditorAssetContextCommand::AddPointLight
        || hoveredCommand == EditorAssetContextCommand::AddSpotLight;
    if (!showLightingSubmenu) {
        return;
    }

    const auto addIter = std::ranges::find_if(items, [](const EditorAssetContextMenuItem& item) {
        return item.command == EditorAssetContextCommand::AddLighting;
    });
    if (addIter == items.end()) {
        return;
    }
    const int addIndex = static_cast<int>(std::distance(items.begin(), addIter));
    const RECT addRow = EditorAssetBrowserLayout::ContextMenuItemRect(menu, addIndex);
    RECT submenu{
        addRow.right + kLightingSubmenuGap,
        addRow.top,
        addRow.right + kLightingSubmenuGap + kLightingSubmenuWidth,
        addRow.top + EditorAssetBrowserLayout::ContextMenuPadding * 2
            + kLightingSubmenuRows * EditorAssetBrowserLayout::ContextMenuRowHeight
            + (kLightingSubmenuRows - 1) * EditorAssetBrowserLayout::ContextMenuSeparatorHeight,
    };
    if (submenu.right > content.right) {
        const int width = submenu.right - submenu.left;
        const int height = submenu.bottom - submenu.top;
        const int left = std::clamp(static_cast<int>(menu.left), static_cast<int>(content.left), std::max(static_cast<int>(content.left), static_cast<int>(content.right) - width));
        int top = menu.bottom + kLightingSubmenuGap;
        if (top + height > content.bottom) {
            top = menu.top - height - kLightingSubmenuGap;
        }
        top = std::clamp(top, static_cast<int>(content.top), std::max(static_cast<int>(content.top), static_cast<int>(content.bottom) - height));
        submenu = RECT{ left, top, left + width, top + height };
    } else if (submenu.bottom > content.bottom) {
        OffsetRect(&submenu, 0, content.bottom - submenu.bottom);
    }

    RECT submenuShadow = submenu;
    OffsetRect(&submenuShadow, 3, 3);
    GdiDrawing::FillRectAlpha(dc, submenuShadow, RGB(0, 0, 0), 90);
    EditorDialogStyle::PaintSurface(dc, submenu, theme);

    struct LightingRow {
        EditorAssetContextCommand command;
        const char* label;
    };
    constexpr LightingRow rows[] = {
        { EditorAssetContextCommand::AddLighting, "Lighting" },
        { EditorAssetContextCommand::AddDirectionalLight, "Directional Light" },
        { EditorAssetContextCommand::AddPointLight, "Point Light" },
        { EditorAssetContextCommand::AddSpotLight, "Spot Light" },
    };
    for (int index = 0; index < kLightingSubmenuRows; ++index) {
        const RECT row = EditorAssetBrowserLayout::ContextMenuItemRect(submenu, index);
        const bool hovered = hoveredCommand == rows[index].command && rows[index].command != EditorAssetContextCommand::AddLighting;
        EditorDialogStyle::PaintMenuRow(
            dc,
            row,
            theme,
            rows[index].label,
            HeroIconKind::Cube,
            hovered,
            true,
            false);
        if (index == 0) {
            RECT separator{ submenu.left + 8, row.bottom + 3, submenu.right - 8, row.bottom + 4 };
            EditorDialogStyle::PaintDivider(dc, separator, theme);
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
    EditorDialogStyle::PaintSurface(dc, menu, theme);

    constexpr const char* labels[2] = { "Move Here", "Copy Here" };
    constexpr EditorAssetDropAction commands[2] = { EditorAssetDropAction::MoveHere, EditorAssetDropAction::CopyHere };
    for (int index = 0; index < 2; ++index) {
        const RECT row = EditorAssetBrowserLayout::ContextMenuItemRect(menu, index);
        const bool hovered = state.DropActionHoveredCommand() == commands[index];
        EditorDialogStyle::PaintMenuRow(dc, row, theme, labels[index], HeroIconKind::Cube, hovered, true, false);
        if (index == 0) {
            RECT separator{ menu.left + 8, row.bottom + 3, menu.right - 8, row.bottom + 4 };
            EditorDialogStyle::PaintDivider(dc, separator, theme);
        }
    }
}

void DrawModalButton(HDC dc, RECT rect, const EditorTheme& theme, const char* text, bool destructive) {
    EditorDialogStyle::PaintButton(
        dc,
        rect,
        theme,
        text,
        destructive ? EditorDialogButtonTone::Destructive : EditorDialogButtonTone::Neutral);
}

[[nodiscard]] RECT DeleteConfirmListRectForDialog(const RECT& dialog) noexcept {
    return RECT{ dialog.left + 24, dialog.top + 108, dialog.right - 24, dialog.bottom - 62 };
}

[[nodiscard]] int DeleteConfirmMaxScrollForList(const RECT& list, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const RECT viewport = SummaryViewportRect(list);
    const int contentHeight = static_cast<int>(state.DeleteTargetRows(manager).size()) * kSummaryRowHeight;
    return std::max(0, contentHeight - Height(viewport));
}

void DrawDeleteConfirmDialogRect(HDC dc, const RECT& dialog, const EditorTheme& theme, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager, bool shadowed) {
    if (shadowed) {
        RECT shadow = dialog;
        OffsetRect(&shadow, 6, 8);
        GdiDrawing::FillRectAlpha(dc, shadow, RGB(0, 0, 0), 155);
    }
    EditorDialogStyle::PaintSurface(dc, dialog, theme);
    RECT header{
        dialog.left + 1,
        dialog.top + 1,
        dialog.right - 1,
        dialog.top + EditorDialogStyle::HeaderHeight,
    };

    const std::vector<EditorAssetSelectionSummaryRow> deleteRows = state.DeleteTargetRows(manager);
    const bool deletingAsset = state.SelectionKind() == EditorAssetBrowserSelectionKind::Asset;
    const std::string titleText = deleteRows.size() == 1U ? "Delete selected object?" : ("Delete selected objects (" + std::to_string(deleteRows.size()) + ")?");
    EditorDialogStyle::PaintHeader(
        dc,
        theme,
        EditorDialogHeaderDescriptor{
            .bounds = header,
            .title = titleText,
            .icon = deletingAsset ? HeroIconKind::Cube : HeroIconKind::Folder,
            .showIcon = true,
        });

    RECT body{ dialog.left + 24, dialog.top + 84, dialog.right - 24, dialog.top + 104 };
    Draw::DrawTextWithFont(dc, body, "This action will permanently remove the listed object(s) from Project Files.", Draw::Color(theme.textSecondary), 12, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const RECT list = DeleteConfirmListRectForDialog(dialog);
    DrawSelectionSummary(dc, list, theme, deleteRows, state.DeleteConfirmListScrollOffset(), DeleteConfirmMaxScrollForList(list, state, manager));

    DrawModalButton(dc, EditorAssetBrowserGeometry::DeleteConfirmAcceptRect(dialog), theme, "Delete", true);
    DrawModalButton(dc, EditorAssetBrowserGeometry::DeleteConfirmCancelRect(dialog), theme, "Cancel", false);
}

void DrawDeleteConfirmDialog(HDC dc, const RECT& bounds, const EditorTheme& theme, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager, bool shadowed) {
    const RECT dialog = EditorAssetBrowserGeometry::DeleteConfirmRect(bounds, state.DeleteConfirmOffsetX(), state.DeleteConfirmOffsetY());
    DrawDeleteConfirmDialogRect(dc, dialog, theme, state, manager, shadowed);
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

int ProjectFilesOverlayRenderer::DeleteConfirmMaxScroll(const RECT& bounds, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserGeometry::DeleteConfirmListMaxScroll(bounds, state, manager);
}

RECT ProjectFilesOverlayRenderer::DeleteConfirmListRect(const RECT& bounds, const EditorAssetBrowserState& state) noexcept {
    return EditorAssetBrowserGeometry::DeleteConfirmListRect(bounds, state);
}

RECT ProjectFilesOverlayRenderer::DeleteConfirmListViewportRect(const RECT& bounds, const EditorAssetBrowserState& state) noexcept {
    return EditorAssetBrowserGeometry::DeleteConfirmListViewportRect(bounds, state);
}

RECT ProjectFilesOverlayRenderer::DeleteConfirmListScrollbarTrackRect(const RECT& bounds, const EditorAssetBrowserState& state) noexcept {
    return EditorAssetBrowserGeometry::DeleteConfirmListScrollbarTrackRect(bounds, state);
}

RECT ProjectFilesOverlayRenderer::DeleteConfirmListScrollbarThumbRect(const RECT& bounds, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserGeometry::DeleteConfirmListScrollbarThumbRect(bounds, state, manager);
}

int ProjectFilesOverlayRenderer::DeleteConfirmListRowHeight() noexcept {
    return EditorAssetBrowserGeometry::DeleteConfirmListRowHeight();
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

void ProjectFilesOverlayRenderer::PaintDeleteConfirmDialogAt(
    HDC dc,
    const RECT& dialog,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    if (!state.IsDeleteConfirmOpen()) {
        return;
    }

    DrawDeleteConfirmDialogRect(dc, dialog, theme, state, manager, false);
}

} // namespace kb::editor

#endif
