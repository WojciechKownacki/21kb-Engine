#include "rendering/ProjectFilesAssetListRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;

void DrawListHeader(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme) {
    RECT header{ layout.assetView.left + 10, layout.assetView.top + 4, layout.assetView.right - 10, layout.assetView.top + EditorAssetBrowserLayout::AssetHeaderHeight };
    Draw::DrawLabel(dc, header, "Name", Draw::Color(theme.textSecondary));
    RECT type{ header.left + 270, header.top, header.left + 410, header.bottom };
    Draw::DrawLabel(dc, type, "Type", Draw::Color(theme.textSecondary));
    RECT path{ header.left + 420, header.top, header.right, header.bottom };
    Draw::DrawLabel(dc, path, "Path", Draw::Color(theme.textSecondary));
    RECT line{ layout.assetView.left + 1, header.bottom, layout.assetView.right - 1, header.bottom + 1 };
    Draw::DrawHairline(dc, line, Draw::Color(theme.borderPanel));
}

void DrawFolderRow(HDC dc, RECT row, const EditorTheme& theme, const EditorAssetFolderRow& folder, const EditorAssetBrowserState& state) {
    if (folder.selected) {
        GdiDrawing::FillRectAlpha(dc, row, Draw::Color(theme.accent), 78);
    }
    RECT icon{ row.left + 6, row.top + 4, row.left + 24, row.bottom - 4 };
    HeroIconPainter::Draw(dc, icon, HeroIconKind::Folder, Draw::FolderColor(folder.selected), 1);
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
        GdiDrawing::FillRectAlpha(dc, row, Draw::Color(theme.accent), 78);
    }
    RECT icon{ row.left + 6, row.top + 4, row.left + 22, row.bottom - 4 };
    HeroIconPainter::Draw(dc, icon, HeroIconKind::Cube, asset.selected ? Draw::Color(theme.accent) : Draw::Color(theme.textSecondary), 2);
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

} // namespace

void ProjectFilesAssetListRenderer::Paint(
    HDC dc,
    const EditorAssetBrowserLayoutRects& layout,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const std::vector<EditorAssetFolderRow>& folders,
    const std::vector<EditorAssetItemRow>& assets) {
    DrawListHeader(dc, layout, theme);
    int rowIndex = 0;
    for (const EditorAssetFolderRow& folder : folders) {
        RECT row = EditorAssetBrowserLayout::AssetListRowRect(layout, rowIndex++);
        if (row.top >= layout.assetView.bottom - 4) {
            return;
        }
        DrawFolderRow(dc, row, theme, folder, state);
    }

    if (state.TextEditMode() == EditorAssetTextEditMode::NewFolder) {
        RECT row = EditorAssetBrowserLayout::AssetListRowRect(layout, rowIndex++);
        RECT field{ row.left + 36, row.top + 2, row.left + 268, row.bottom - 2 };
        Draw::DrawEditField(dc, field, theme, state.TextEditValue());
    }

    for (const EditorAssetItemRow& asset : assets) {
        RECT row = EditorAssetBrowserLayout::AssetListRowRect(layout, rowIndex++);
        if (row.top >= layout.assetView.bottom - 4) {
            return;
        }
        DrawAssetRow(dc, row, theme, asset, state);
    }
}

} // namespace kb::editor

#endif
