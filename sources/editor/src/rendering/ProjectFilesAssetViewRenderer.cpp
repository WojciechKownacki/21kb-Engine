#include "rendering/ProjectFilesAssetViewRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectFilesAssetListRenderer.hpp"
#include "rendering/ProjectFilesAssetTileRenderer.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"

namespace kb::editor {

void ProjectFilesAssetViewRenderer::Paint(
    HDC dc,
    const EditorAssetBrowserLayoutRects& layout,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const std::vector<EditorAssetFolderRow>& folders,
    const std::vector<EditorAssetItemRow>& assets) {
    using Draw = ProjectFilesPanelDrawing;

    GdiDrawing::DrawSharpFrame(dc, layout.assetView, Draw::Blend(Draw::Color(theme.panel), Draw::Color(theme.strip), 12), Draw::Color(theme.borderPanel));
    RECT innerTop{ layout.assetView.left + 1, layout.assetView.top + 1, layout.assetView.right - 1, layout.assetView.top + 2 };
    GdiDrawing::FillRectAlpha(dc, innerTop, RGB(255, 255, 255), 10);
    if (state.ViewMode() == EditorAssetViewMode::Tiles) {
        ProjectFilesAssetTileRenderer::Paint(dc, layout, theme, state, folders, assets);
    } else {
        ProjectFilesAssetListRenderer::Paint(dc, layout, theme, state, folders, assets);
    }
}

} // namespace kb::editor

#endif
