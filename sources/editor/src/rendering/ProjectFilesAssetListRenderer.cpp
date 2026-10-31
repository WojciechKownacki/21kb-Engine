#include "rendering/ProjectFilesAssetListRenderer.hpp"
#include "localization/EditorLocalization.hpp"

#if defined(_WIN32)
#include "app/EditorAudioAssetPreview.hpp"
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

// The "Used by" column takes the right end of a row wide enough to keep the path readable.
constexpr int kUsedByWidth = 220;
constexpr int kUsedByMinimumRowWidth = 900;

[[nodiscard]] bool ShowsUsedBy(const RECT& row) noexcept {
    return row.right - row.left >= kUsedByMinimumRowWidth;
}

[[nodiscard]] RECT PathRect(const RECT& row) noexcept {
    return RECT{ row.left + 420, row.top, ShowsUsedBy(row) ? row.right - kUsedByWidth - 10 : row.right, row.bottom };
}

[[nodiscard]] RECT UsedByRect(const RECT& row) noexcept {
    return RECT{ row.right - kUsedByWidth, row.top, row.right - 8, row.bottom };
}

// The first two objects by name and how many more, so the column stays one line.
[[nodiscard]] std::string UsedByText(const EditorAssetUsers& users, std::uint64_t assetId) {
    const auto found = users.find(assetId);
    if (found == users.end() || found->second.empty()) {
        return {};
    }
    const std::vector<std::string>& names = found->second;
    std::string text = names[0];
    if (names.size() > 1U) {
        text += ", " + names[1];
    }
    if (names.size() > 2U) {
        text += " +" + std::to_string(names.size() - 2U);
    }
    return text;
}

void DrawListHeader(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme) {
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    RECT header{ viewport.left, viewport.top, viewport.right, viewport.top + EditorAssetBrowserLayout::AssetHeaderHeight };
    Draw::DrawLabel(dc, header, EditorText("projectFiles.list.name", "Name"), Draw::Color(theme.textSecondary));
    RECT type{ header.left + 270, header.top, header.left + 410, header.bottom };
    Draw::DrawLabel(dc, type, EditorText("projectFiles.list.type", "Type"), Draw::Color(theme.textSecondary));
    Draw::DrawLabel(dc, PathRect(header), EditorText("projectFiles.list.path", "Path"), Draw::Color(theme.textSecondary));
    if (ShowsUsedBy(header)) {
        Draw::DrawLabel(dc, UsedByRect(header), EditorText("projectFiles.list.usedBy", "Used by"), Draw::Color(theme.textSecondary));
    }
    RECT line{ viewport.left, header.bottom, viewport.right, header.bottom + 1 };
    Draw::DrawHairline(dc, line, Draw::Color(theme.borderPanel));
}

void DrawFolderRow(HDC dc, RECT row, const EditorTheme& theme, const EditorAssetFolderRow& folder, const EditorAssetBrowserState& state) {
    if (folder.selected) {
        GdiDrawing::FillRectColor(dc, row, Draw::Blend(Draw::Color(theme.panel), Draw::Color(theme.accent), state.IsSelectionFocused() ? 18 : 10));
        if (state.IsSelectionFocused()) {
            GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Draw::Color(theme.accent));
        }
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
    Draw::DrawLabel(dc, type, EditorText("projectFiles.list.folder", "Folder"), Draw::Color(theme.textSecondary));
    Draw::DrawLabel(dc, PathRect(row), kb::assets::NormalizeAssetPath(folder.virtualPath).c_str(), Draw::Color(theme.textDisabled));
}

void DrawAssetRow(HDC dc, RECT row, const EditorTheme& theme, const EditorAssetItemRow& asset, const EditorAssetBrowserState& state,
    const EditorAssetUsers& users) {
    if (asset.selected) {
        GdiDrawing::FillRectColor(dc, row, Draw::Blend(Draw::Color(theme.panel), Draw::Color(theme.accent), state.IsSelectionFocused() ? 18 : 10));
        if (state.IsSelectionFocused()) {
            GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Draw::Color(theme.accent));
        }
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
    if (EditorAudioAssetPreview::IsPlaying(asset.metadata.id)) {
        Draw::DrawLabel(dc, type, EditorText("projectFiles.list.playing", "Playing"), RGB(116, 232, 205));
    } else {
        Draw::DrawLabel(dc, type, asset.metadata.type.c_str(), Draw::Color(theme.textSecondary));
    }
    Draw::DrawLabel(dc, PathRect(row), kb::assets::NormalizeAssetPath(asset.metadata.virtualPath).c_str(), Draw::Color(theme.textDisabled));
    if (ShowsUsedBy(row)) {
        const std::string usedBy = UsedByText(users, asset.metadata.id.value);
        Draw::DrawLabel(dc, UsedByRect(row), usedBy.empty() ? "-" : usedBy.c_str(),
            Draw::Color(usedBy.empty() ? theme.textDisabled : theme.textSecondary));
    }
}

[[nodiscard]] int TotalRowCount(const EditorAssetBrowserState& state, const std::vector<EditorAssetFolderRow>& folders, const std::vector<EditorAssetItemRow>& assets) noexcept {
    return static_cast<int>(folders.size() + assets.size() + (state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1U : 0U));
}

void DrawScrollbar(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme, const EditorAssetBrowserState& state, int contentHeight) {
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    const int viewportHeight = static_cast<int>(viewport.bottom - viewport.top - EditorAssetBrowserLayout::AssetHeaderHeight);
    if (contentHeight <= viewportHeight) {
        return;
    }
    RECT track = EditorAssetBrowserLayout::AssetScrollbarTrackRect(layout);
    track.top += EditorAssetBrowserLayout::AssetHeaderHeight;
    const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, state.ContentScrollOffset());
    GdiDrawing::DrawSharpFrame(dc, track, Draw::Color(theme.chrome), Draw::Color(theme.borderChrome));
    const COLORREF thumbColor = Draw::Color(state.IsContentScrollbarDragging() ? theme.accent : theme.borderPanel);
    const COLORREF thumbBorder = Draw::Color(state.IsContentScrollbarDragging() ? theme.textSecondary : theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, thumb, thumbColor, thumbBorder);
}

} // namespace

void ProjectFilesAssetListRenderer::Paint(
    HDC dc,
    const EditorAssetBrowserLayoutRects& layout,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const std::vector<EditorAssetFolderRow>& folders,
    const std::vector<EditorAssetItemRow>& assets,
    const EditorAssetUsers& users) {
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
            DrawAssetRow(dc, row, theme, assets[static_cast<std::size_t>(relative)], state, users);
        }
    }
    RestoreDC(dc, -1);
    DrawScrollbar(dc, layout, theme, state, contentHeight);
}

} // namespace kb::editor

#endif
