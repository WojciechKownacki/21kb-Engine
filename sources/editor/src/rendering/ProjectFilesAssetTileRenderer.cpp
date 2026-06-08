#include "rendering/ProjectFilesAssetTileRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "rendering/EditorMeshThumbnailService.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectFilesAssetTileFrameRenderer.hpp"
#include "rendering/ProjectFilesAssetTileMetrics.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/ProjectFilesTileTextRenderer.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/gdi/ScopedPen.hpp"

#include <algorithm>
#include <cstdint>

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;
using Frame = ProjectFilesAssetTileFrameRenderer;
using Metrics = ProjectFilesAssetTileMetrics;
using Text = ProjectFilesTileTextRenderer;

[[nodiscard]] bool IsPrefabAsset(const EditorAssetItemRow& asset) {
    return asset.metadata.type == "ScenePrefab" || asset.metadata.virtualPath.extension() == ".kbprefab";
}

[[nodiscard]] COLORREF AssetIconColor(const EditorAssetItemRow& asset) {
    if (IsPrefabAsset(asset)) {
        return asset.selected ? RGB(106, 177, 255) : RGB(68, 145, 236);
    }
    return asset.selected ? RGB(205, 211, 221) : RGB(174, 181, 193);
}

[[nodiscard]] RECT ThumbnailRect(const RECT& tile, const ProjectFilesAssetTileVisualLayout& visual) noexcept {
    const int width = Draw::RectWidth(tile);
    const int availableHeight = std::max(1, static_cast<int>(visual.label.top - tile.top - 11));
    const int maximumSize = std::max(24, std::min(width - 26, availableHeight));
    const int size = std::min(maximumSize, 86);
    const int left = tile.left + (width - size) / 2;
    const int top = tile.top + std::max(7, (availableHeight - size) / 2 + 5);
    return RECT{ left, top, left + size, top + size };
}

void DrawThumbnailBitmap(HDC dc, const RECT& target, const EditorMeshThumbnailImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty() || target.right <= target.left || target.bottom <= target.top) {
        return;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const int oldMode = SetStretchBltMode(dc, HALFTONE);
    static_cast<void>(StretchDIBits(
        dc,
        target.left,
        target.top,
        Draw::RectWidth(target),
        Draw::RectHeight(target),
        0,
        0,
        image.width,
        image.height,
        image.bgra.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY));
    SetStretchBltMode(dc, oldMode);

    ScopedPen border{ 1, RGB(52, 59, 68) };
    const ScopedGdiObject selectedPen(dc, border.handle);
    const ScopedGdiObject selectedBrush(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, target.left, target.top, target.right, target.bottom);
}

void DrawFolderTile(HDC dc, RECT tile, const EditorTheme& theme, const EditorAssetFolderRow& folder, bool highlighted, const EditorAssetBrowserState& state) {
    Frame::Paint(dc, tile, theme, highlighted, highlighted && state.IsSelectionFocused());
    const int namePoint = Metrics::NamePointSize(tile);
    const ProjectFilesAssetTileVisualLayout visual = Metrics::ResolveVisualLayout(tile);
    Draw::DrawIconWithShadow(dc, visual.icon, HeroIconKind::Folder, Draw::FolderColor(highlighted), 1);
    if (state.TextEditMode() == EditorAssetTextEditMode::RenameFolder && Draw::SameVirtualPath(folder.virtualPath, state.TextEditTargetFolder())) {
        Draw::DrawCenteredEditField(dc, visual.label, theme, state.TextEditValue());
    } else {
        Text::PaintWrapped(dc, visual.label, folder.name.c_str(), highlighted ? Draw::Color(theme.textPrimary) : Draw::Blend(Draw::Color(theme.textPrimary), Draw::Color(theme.textSecondary), 18), namePoint, FW_MEDIUM);
    }
}

void DrawAssetTile(HDC dc, RECT tile, const EditorTheme& theme, const EditorAssetItemRow& asset, const EditorAssetBrowserState& state, EditorMeshThumbnailService& meshThumbnails) {
    Frame::Paint(dc, tile, theme, asset.selected, asset.selected && state.IsSelectionFocused());
    const int namePoint = Metrics::NamePointSize(tile);
    const ProjectFilesAssetTileVisualLayout visual = Metrics::ResolveVisualLayout(tile);
    if (const EditorMeshThumbnailImage* thumbnail = meshThumbnails.ThumbnailFor(asset.metadata)) {
        DrawThumbnailBitmap(dc, ThumbnailRect(tile, visual), *thumbnail);
    } else {
        Draw::DrawIconWithShadow(dc, visual.icon, HeroIconKind::Cube, AssetIconColor(asset), 2);
    }
    if (state.TextEditMode() == EditorAssetTextEditMode::RenameAsset && state.TextEditTargetAsset() == asset.metadata.id) {
        Draw::DrawCenteredEditField(dc, visual.label, theme, state.TextEditValue());
    } else {
        Text::PaintWrapped(dc, visual.label, asset.metadata.name.c_str(), Draw::Color(theme.textPrimary), namePoint, FW_MEDIUM);
    }
}

void DrawNewFolderTile(HDC dc, RECT tile, const EditorTheme& theme, const EditorAssetBrowserState& state) {
    Frame::Paint(dc, tile, theme, true);
    const ProjectFilesAssetTileVisualLayout visual = Metrics::ResolveVisualLayout(tile);
    Draw::DrawIconWithShadow(dc, visual.icon, HeroIconKind::Folder, Draw::FolderColor(true), 1);
    Draw::DrawCenteredEditField(dc, visual.label, theme, state.TextEditValue());
}

[[nodiscard]] int TotalTileCount(const EditorAssetBrowserState& state, const std::vector<EditorAssetFolderRow>& folders, const std::vector<EditorAssetItemRow>& assets) noexcept {
    return static_cast<int>(folders.size() + assets.size() + (state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1U : 0U));
}

void DrawScrollbar(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme, const EditorAssetBrowserState& state, int contentHeight) {
    static_cast<void>(theme);
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    const int viewportHeight = static_cast<int>(viewport.bottom - viewport.top);
    if (contentHeight <= viewportHeight) {
        return;
    }
    const RECT track = EditorAssetBrowserLayout::AssetScrollbarTrackRect(layout);
    const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, state.ContentScrollOffset());
    GdiDrawing::DrawSharpFrame(dc, track, RGB(22, 24, 27), RGB(38, 42, 47));
    const COLORREF thumbColor = state.IsContentScrollbarDragging() ? RGB(104, 116, 130) : RGB(76, 86, 98);
    const COLORREF thumbBorder = state.IsContentScrollbarDragging() ? RGB(128, 142, 158) : RGB(94, 105, 118);
    GdiDrawing::DrawSharpFrame(dc, thumb, thumbColor, thumbBorder);
}

} // namespace

void ProjectFilesAssetTileRenderer::Paint(
    HDC dc,
    const EditorAssetBrowserLayoutRects& layout,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    EditorMeshThumbnailService& meshThumbnails,
    const std::vector<EditorAssetFolderRow>& folders,
    const std::vector<EditorAssetItemRow>& assets) {
    constexpr int tileGap = 5;
    const int columns = EditorAssetBrowserLayout::AssetTileColumnCount(layout, state.ThumbnailScale());
    const int stepY = EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) + tileGap;
    const int totalItems = TotalTileCount(state, folders, assets);
    const int totalRows = (totalItems + columns - 1) / columns;
    const int contentHeight = totalRows * stepY;
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    const int maxOffset = std::max(0, contentHeight - static_cast<int>(viewport.bottom - viewport.top));
    const int scroll = std::clamp(state.ContentScrollOffset(), 0, maxOffset);
    const int firstIndex = std::clamp((scroll / stepY) * columns, 0, totalItems);
    const int visibleRows = (static_cast<int>(viewport.bottom - viewport.top) / stepY) + 3;
    const int lastIndex = std::clamp(firstIndex + visibleRows * columns, 0, totalItems);

    SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    for (int globalIndex = firstIndex; globalIndex < lastIndex; ++globalIndex) {
        RECT tile = EditorAssetBrowserLayout::AssetTileRect(layout, globalIndex, state.ThumbnailScale());
        OffsetRect(&tile, 0, -scroll);
        if (globalIndex < static_cast<int>(folders.size())) {
            const EditorAssetFolderRow& folder = folders[static_cast<std::size_t>(globalIndex)];
            const bool highlighted = folder.selected
                || (state.ContextMenuTargetKind() == EditorAssetContextTargetKind::Folder
                    && Draw::SameVirtualPath(state.ContextMenuTargetFolder(), folder.virtualPath));
            DrawFolderTile(dc, tile, theme, folder, highlighted, state);
            continue;
        }
        int relative = globalIndex - static_cast<int>(folders.size());
        if (state.TextEditMode() == EditorAssetTextEditMode::NewFolder) {
            if (relative == 0) {
                DrawNewFolderTile(dc, tile, theme, state);
                continue;
            }
            --relative;
        }
        if (relative >= 0 && relative < static_cast<int>(assets.size())) {
            DrawAssetTile(dc, tile, theme, assets[static_cast<std::size_t>(relative)], state, meshThumbnails);
        }
    }
    RestoreDC(dc, -1);
    DrawScrollbar(dc, layout, theme, state, contentHeight);
}

} // namespace kb::editor

#endif
