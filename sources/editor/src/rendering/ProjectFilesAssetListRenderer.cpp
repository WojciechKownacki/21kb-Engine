#include "rendering/ProjectFilesAssetListRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ProjectFilesAssetIconResolver.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;

void DrawListHeader(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme) {
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    RECT header{ viewport.left, viewport.top, viewport.right, viewport.top + EditorAssetBrowserLayout::AssetHeaderHeight };
    Draw::DrawLabel(dc, header, "Name", Draw::Color(theme.textSecondary));
    RECT type{ header.left + 270, header.top, header.left + 410, header.bottom };
    Draw::DrawLabel(dc, type, "Type", Draw::Color(theme.textSecondary));
    RECT path{ header.left + 420, header.top, header.right, header.bottom };
    Draw::DrawLabel(dc, path, "Path", Draw::Color(theme.textSecondary));
    RECT line{ viewport.left, header.bottom, viewport.right, header.bottom + 1 };
    Draw::DrawHairline(dc, line, Draw::Color(theme.borderPanel));
}

void DrawFolderRow(HDC dc, RECT row, const EditorTheme& theme, const EditorAssetFolderRow& folder, const EditorAssetBrowserState& state) {
    if (folder.selected) {
        GdiDrawing::FillRectColor(dc, row, Draw::Blend(Draw::Color(theme.panel), RGB(96, 108, 126), state.IsSelectionFocused() ? 34 : 20));
    }
    RECT icon{ row.left + 6, row.top + 4, row.left + 24, row.bottom - 4 };
    Draw::DrawIconWithShadow(dc, icon, HeroIconKind::Folder, Draw::FolderColor(folder.selected), 1);
    RECT name{ icon.right + 8, row.top, row.left + 268, row.bottom };
    if (state.TextEditMode() == EditorAssetTextEditMode::RenameFolder && Draw::SameVirtualPath(folder.virtualPath, state.TextEditTargetFolder())) {
        Draw::DrawEditField(dc, name, theme, state.TextEditValue());
    } else {
        Draw::DrawLabel(dc, name, folder.name.c_str(), Draw::Color(theme.textPrimary));
    }
    RECT type{ row.left + 270, row.top, row.left + 410, row.bottom };
    Draw::DrawLabel(dc, type, "Folder", Draw::Color(theme.textSecondary));
    RECT path{ row.left + 420, row.top, row.right, row.bottom };
    Draw::DrawLabel(dc, path, kb::assets::NormalizeAssetPath(folder.virtualPath).c_str(), Draw::Color(theme.textDisabled));
}

void DrawAssetRow(HDC dc, RECT row, const EditorTheme& theme, const EditorAssetItemRow& asset, const EditorAssetBrowserState& state) {
    if (asset.selected) {
        GdiDrawing::FillRectColor(dc, row, Draw::Blend(Draw::Color(theme.panel), RGB(96, 108, 126), state.IsSelectionFocused() ? 34 : 20));
    }
    RECT icon{ row.left + 6, row.top + 4, row.left + 22, row.bottom - 4 };
    const ProjectFilesAssetIcon assetIcon = ProjectFilesAssetIconResolver::Resolve(asset.metadata, asset.selected);
    Draw::DrawIconWithShadow(dc, icon, assetIcon.kind, assetIcon.color, assetIcon.strokeWidth);
    RECT name{ icon.right + 8, row.top, row.left + 268, row.bottom };
    if (state.TextEditMode() == EditorAssetTextEditMode::RenameAsset && state.TextEditTargetAsset() == asset.metadata.id) {
        Draw::DrawEditField(dc, name, theme, state.TextEditValue());
    } else {
        Draw::DrawLabel(dc, name, asset.metadata.name.c_str(), Draw::Color(theme.textPrimary));
    }
    RECT type{ row.left + 270, row.top, row.left + 410, row.bottom };
    Draw::DrawLabel(dc, type, asset.metadata.type.c_str(), Draw::Color(theme.textSecondary));
    RECT path{ row.left + 420, row.top, row.right, row.bottom };
    Draw::DrawLabel(dc, path, kb::assets::NormalizeAssetPath(asset.metadata.virtualPath).c_str(), Draw::Color(theme.textDisabled));
}

[[nodiscard]] int TotalRowCount(const EditorAssetBrowserState& state, const std::vector<EditorAssetFolderRow>& folders, const std::vector<EditorAssetItemRow>& assets) noexcept {
    return static_cast<int>(folders.size() + assets.size() + (state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1U : 0U));
}

void DrawScrollbar(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme, const EditorAssetBrowserState& state, int contentHeight) {
    static_cast<void>(theme);
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    const int viewportHeight = static_cast<int>(viewport.bottom - viewport.top - EditorAssetBrowserLayout::AssetHeaderHeight);
    if (contentHeight <= viewportHeight) {
        return;
    }
    RECT track = EditorAssetBrowserLayout::AssetScrollbarTrackRect(layout);
    track.top += EditorAssetBrowserLayout::AssetHeaderHeight;
    const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, state.ContentScrollOffset());
    GdiDrawing::DrawSharpFrame(dc, track, RGB(22, 24, 27), RGB(38, 42, 47));
    const COLORREF thumbColor = state.IsContentScrollbarDragging() ? RGB(104, 116, 130) : RGB(76, 86, 98);
    const COLORREF thumbBorder = state.IsContentScrollbarDragging() ? RGB(128, 142, 158) : RGB(94, 105, 118);
    GdiDrawing::DrawSharpFrame(dc, thumb, thumbColor, thumbBorder);
}

} // namespace

void ProjectFilesAssetListRenderer::Paint(
    HDC dc,
    const EditorAssetBrowserLayoutRects& layout,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const std::vector<EditorAssetFolderRow>& folders,
    const std::vector<EditorAssetItemRow>& assets) {
    DrawListHeader(dc, layout, theme);
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    const int totalRows = TotalRowCount(state, folders, assets);
    const int contentHeight = totalRows * EditorAssetBrowserLayout::RowHeight;
    const int bodyHeight = static_cast<int>(viewport.bottom - viewport.top - EditorAssetBrowserLayout::AssetHeaderHeight);
    const int maxOffset = std::max(0, contentHeight - bodyHeight);
    const int scroll = std::clamp(state.ContentScrollOffset(), 0, maxOffset);
    const int firstRow = std::max(0, scroll / EditorAssetBrowserLayout::RowHeight);
    const int visibleRows = (bodyHeight / EditorAssetBrowserLayout::RowHeight) + 3;
    const int lastRow = std::clamp(firstRow + visibleRows, 0, totalRows);

    SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top + EditorAssetBrowserLayout::AssetHeaderHeight, viewport.right, viewport.bottom);
    for (int globalRow = firstRow; globalRow < lastRow; ++globalRow) {
        RECT row = EditorAssetBrowserLayout::AssetListRowRect(layout, globalRow);
        OffsetRect(&row, 0, -scroll);
        if (globalRow < static_cast<int>(folders.size())) {
            DrawFolderRow(dc, row, theme, folders[static_cast<std::size_t>(globalRow)], state);
            continue;
        }
        int relative = globalRow - static_cast<int>(folders.size());
        if (state.TextEditMode() == EditorAssetTextEditMode::NewFolder) {
            if (relative == 0) {
                RECT field{ row.left + 36, row.top + 2, row.left + 268, row.bottom - 2 };
                Draw::DrawEditField(dc, field, theme, state.TextEditValue());
                continue;
            }
            --relative;
        }
        if (relative >= 0 && relative < static_cast<int>(assets.size())) {
            DrawAssetRow(dc, row, theme, assets[static_cast<std::size_t>(relative)], state);
        }
    }
    RestoreDC(dc, -1);
    DrawScrollbar(dc, layout, theme, state, contentHeight);
}

} // namespace kb::editor

#endif
