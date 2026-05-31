#include "rendering/ProjectFilesAssetTileRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "rendering/ProjectFilesAssetTileFrameRenderer.hpp"
#include "rendering/ProjectFilesAssetTileMetrics.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/ProjectFilesTileTextRenderer.hpp"

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;
using Frame = ProjectFilesAssetTileFrameRenderer;
using Metrics = ProjectFilesAssetTileMetrics;
using Text = ProjectFilesTileTextRenderer;

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

void DrawAssetTile(HDC dc, RECT tile, const EditorTheme& theme, const EditorAssetItemRow& asset, const EditorAssetBrowserState& state) {
    Frame::Paint(dc, tile, theme, asset.selected, asset.selected && state.IsSelectionFocused());
    const int namePoint = Metrics::NamePointSize(tile);
    const ProjectFilesAssetTileVisualLayout visual = Metrics::ResolveVisualLayout(tile);
    Draw::DrawIconWithShadow(dc, visual.icon, HeroIconKind::Cube, asset.selected ? RGB(205, 211, 221) : RGB(174, 181, 193), 2);
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

} // namespace

void ProjectFilesAssetTileRenderer::Paint(
    HDC dc,
    const EditorAssetBrowserLayoutRects& layout,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const std::vector<EditorAssetFolderRow>& folders,
    const std::vector<EditorAssetItemRow>& assets) {
    int index = 0;
    for (const EditorAssetFolderRow& folder : folders) {
        RECT tile = EditorAssetBrowserLayout::AssetTileRect(layout, index++, state.ThumbnailScale());
        if (tile.top >= layout.assetView.bottom - 4) {
            return;
        }
        const bool highlighted = folder.selected
            || (state.ContextMenuTargetKind() == EditorAssetContextTargetKind::Folder
                && Draw::SameVirtualPath(state.ContextMenuTargetFolder(), folder.virtualPath));
        DrawFolderTile(dc, tile, theme, folder, highlighted, state);
    }

    if (state.TextEditMode() == EditorAssetTextEditMode::NewFolder) {
        RECT tile = EditorAssetBrowserLayout::AssetTileRect(layout, index++, state.ThumbnailScale());
        DrawNewFolderTile(dc, tile, theme, state);
    }

    for (const EditorAssetItemRow& asset : assets) {
        RECT tile = EditorAssetBrowserLayout::AssetTileRect(layout, index++, state.ThumbnailScale());
        if (tile.top >= layout.assetView.bottom - 4) {
            return;
        }
        DrawAssetTile(dc, tile, theme, asset, state);
    }
}

} // namespace kb::editor

#endif
