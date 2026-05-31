#include "rendering/ProjectFilesPanelRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserLayout.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/ProjectFilesAssetViewRenderer.hpp"
#include "rendering/ProjectFilesBottomBarRenderer.hpp"
#include "rendering/ProjectFilesOverlayRenderer.hpp"
#include "rendering/ProjectFilesToolbarRenderer.hpp"
#include "rendering/ProjectFilesTreeRenderer.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <vector>

namespace kb::editor {

void ProjectFilesPanelRenderer::Paint(HDC dc, const RECT& content, const RECT& overlayBounds, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(content);

    ScopedFont bodyFont{ 13, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, bodyFont.handle);

    const std::vector<EditorAssetFolderRow> treeFolders = state.FolderRows(manager);
    const std::vector<EditorAssetFolderRow> childFolders = state.ChildFolderRows(manager);
    const std::vector<EditorAssetItemRow> assets = state.AssetRows(manager);

    ProjectFilesToolbarRenderer::Paint(dc, layout, theme, state);
    ProjectFilesTreeRenderer::Paint(dc, layout, theme, state, treeFolders);
    ProjectFilesAssetViewRenderer::Paint(dc, layout, theme, state, childFolders, assets);
    ProjectFilesBottomBarRenderer::Paint(dc, layout, theme, state);
    ProjectFilesOverlayRenderer::Paint(dc, content, overlayBounds, theme, state, manager);
}

} // namespace kb::editor

#endif
