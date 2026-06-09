#include "rendering/ProjectFilesPanelRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserLayout.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/EditorMeshThumbnailService.hpp"
#include "rendering/ProjectFilesAssetViewRenderer.hpp"
#include "rendering/ProjectFilesBottomBarRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectFilesOverlayRenderer.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/ProjectFilesToolbarRenderer.hpp"
#include "rendering/ProjectFilesTreeRenderer.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] int Width(const RECT& rect) noexcept {
    return static_cast<int>(rect.right - rect.left);
}

[[nodiscard]] int Height(const RECT& rect) noexcept {
    return static_cast<int>(rect.bottom - rect.top);
}

void AppendRect(std::ostringstream& out, const RECT& rect) {
    out << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom << ';';
}

[[nodiscard]] std::string BuildProjectFilesSignature(
    const RECT& content,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager,
    const EditorMeshThumbnailService& meshThumbnails) {
    std::ostringstream out;
    AppendRect(out, content);
    out << "rev=" << manager.Revision()
        << ";folder=" << kb::assets::NormalizeAssetPath(state.SelectedFolder())
        << ";contentFolder=" << kb::assets::NormalizeAssetPath(state.SelectedContentFolder())
        << ";asset=" << state.SelectedAsset().value
        << ";kind=" << static_cast<int>(state.SelectionKind())
        << ";focus=" << state.IsSelectionFocused()
        << ";search=" << state.SearchQuery()
        << ";recursive=" << state.Recursive()
        << ";view=" << static_cast<int>(state.ViewMode())
        << ";sort=" << static_cast<int>(state.SortMode())
        << ";type=" << state.TypeFilter()
        << ";showFolders=" << state.ShowFolders()
        << ";showTemplates=" << state.ShowTemplates()
        << ";filterOpen=" << state.IsFilterMenuOpen()
        << ";sortOpen=" << state.IsSortMenuOpen()
        << ";scale=" << state.ThumbnailScale()
        << ";scaleDrag=" << state.IsThumbnailScaleDragging()
        << ";treeWidth=" << state.TreeWidth()
        << ";treeDrag=" << state.IsTreeWidthDragging()
        << ";treeScroll=" << state.TreeScrollOffset()
        << ";treeScrollDrag=" << state.IsTreeScrollbarDragging()
        << ";contentScroll=" << state.ContentScrollOffset()
        << ";contentScrollDrag=" << state.IsContentScrollbarDragging()
        << ";editMode=" << static_cast<int>(state.TextEditMode())
        << ";editValue=" << state.TextEditValue()
        << ";editAsset=" << state.TextEditTargetAsset().value
        << ";editFolder=" << kb::assets::NormalizeAssetPath(state.TextEditTargetFolder())
        << ";ctxKind=" << static_cast<int>(state.ContextMenuTargetKind())
        << ";ctxFolder=" << kb::assets::NormalizeAssetPath(state.ContextMenuTargetFolder())
        << ";ctxAsset=" << state.ContextMenuTargetAsset().value
        << ";meshThumbRev=" << meshThumbnails.Revision();
    const std::vector<EditorAssetFolderRow> childFolders = state.ChildFolderRows(manager);
    for (const EditorAssetFolderRow& folder : childFolders) {
        if (folder.selected) {
            out << ";selFolder=" << kb::assets::NormalizeAssetPath(folder.virtualPath);
        }
    }
    const std::vector<EditorAssetItemRow> assets = state.AssetRows(manager);
    for (const EditorAssetItemRow& asset : assets) {
        if (asset.selected) {
            out << ";selAsset=" << asset.metadata.id.value;
        }
    }
    return out.str();
}

class ProjectFilesRetainedSurface {
public:
    ~ProjectFilesRetainedSurface() {
        Reset();
    }

    [[nodiscard]] bool Matches(int width, int height, const std::string& signature) const noexcept {
        return dc_ != nullptr && bitmap_ != nullptr && width_ == width && height_ == height && signature_ == signature;
    }

    [[nodiscard]] HDC BeginRender(HDC target, int width, int height, std::string signature) {
        if (!Ensure(target, width, height)) {
            return nullptr;
        }
        signature_ = std::move(signature);
        return dc_;
    }

    void Blit(HDC target, const RECT& content) const {
        if (dc_ == nullptr) {
            return;
        }
        BitBlt(target, content.left, content.top, width_, height_, dc_, 0, 0, SRCCOPY);
    }

private:
    [[nodiscard]] bool Ensure(HDC target, int width, int height) {
        if (dc_ != nullptr && bitmap_ != nullptr && width_ == width && height_ == height) {
            return true;
        }

        Reset();
        if (width <= 0 || height <= 0) {
            return false;
        }

        dc_ = CreateCompatibleDC(target);
        if (dc_ == nullptr) {
            return false;
        }
        bitmap_ = CreateCompatibleBitmap(target, width, height);
        if (bitmap_ == nullptr) {
            Reset();
            return false;
        }
        oldBitmap_ = static_cast<HBITMAP>(SelectObject(dc_, bitmap_));
        width_ = width;
        height_ = height;
        return true;
    }

    void Reset() noexcept {
        if (dc_ != nullptr) {
            if (oldBitmap_ != nullptr) {
                SelectObject(dc_, oldBitmap_);
                oldBitmap_ = nullptr;
            }
            DeleteDC(dc_);
            dc_ = nullptr;
        }
        if (bitmap_ != nullptr) {
            DeleteObject(bitmap_);
            bitmap_ = nullptr;
        }
        width_ = 0;
        height_ = 0;
        signature_.clear();
    }

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HBITMAP oldBitmap_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::string signature_;
};

void PaintProjectFilesBase(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager,
    EditorMeshThumbnailService& meshThumbnails) {
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(content, state.TreeWidth());

    ScopedFont bodyFont{ 13, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, bodyFont.handle);

    const std::vector<EditorAssetFolderRow> treeFolders = state.FolderRows(manager);
    const std::vector<EditorAssetFolderRow> childFolders = state.ChildFolderRows(manager);
    const std::vector<EditorAssetItemRow> assets = state.AssetRows(manager);

    ProjectFilesToolbarRenderer::Paint(dc, layout, theme, state);
    ProjectFilesTreeRenderer::Paint(dc, layout, theme, state, treeFolders);
    ProjectFilesAssetViewRenderer::Paint(dc, layout, theme, state, meshThumbnails, childFolders, assets);
    RECT splitterLine{ layout.tree.right, layout.tree.top, layout.tree.right + 1, layout.bottomBar.top };
    GdiDrawing::FillRectColor(
        dc,
        splitterLine,
        state.IsTreeWidthDragging()
            ? ProjectFilesPanelDrawing::Blend(ProjectFilesPanelDrawing::Color(theme.borderPanel), ProjectFilesPanelDrawing::Color(theme.accent), 34)
            : ProjectFilesPanelDrawing::Color(theme.borderPanel));
    ProjectFilesBottomBarRenderer::Paint(dc, layout, theme, state);
}

ProjectFilesRetainedSurface& RetainedSurface() {
    static ProjectFilesRetainedSurface surface;
    return surface;
}

} // namespace

void ProjectFilesPanelRenderer::Paint(HDC dc, const RECT& content, const RECT& overlayBounds, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    EditorMeshThumbnailService& meshThumbnails = EditorMeshThumbnailCache();

    const int width = Width(content);
    const int height = Height(content);
    const std::string signature = BuildProjectFilesSignature(content, state, manager, meshThumbnails);
    ProjectFilesRetainedSurface& surface = RetainedSurface();
    if (!surface.Matches(width, height, signature)) {
        HDC cachedDc = surface.BeginRender(dc, width, height, signature);
        if (cachedDc != nullptr) {
            const int savedDc = SaveDC(cachedDc);
            SetViewportOrgEx(cachedDc, -content.left, -content.top, nullptr);
            PaintProjectFilesBase(cachedDc, content, theme, state, manager, meshThumbnails);
            RestoreDC(cachedDc, savedDc);
        } else {
            PaintProjectFilesBase(dc, content, theme, state, manager, meshThumbnails);
        }
    }
    surface.Blit(dc, content);
    ProjectFilesOverlayRenderer::Paint(dc, content, overlayBounds, theme, state, manager);
}

} // namespace kb::editor

#endif
