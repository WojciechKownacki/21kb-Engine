#include "app/EditorAssetBrowserPrimaryClickHandler.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserLayout.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "platform/win32/EditorAssetImportDialog.hpp"
#include "scene/EditorSceneContext.hpp"

#include <filesystem>
#include <algorithm>
#include <optional>
#include <vector>

namespace kb::editor {
namespace {

void PrepareBrowserAction(EditorAssetBrowserState& state) noexcept {
    state.FocusSearch(false);
    state.CancelTextEdit();
}

bool ExecuteDropAction(EditorSceneContext& sceneContext, EditorAssetDropAction action) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    const kb::assets::AssetId asset = state.DropActionAsset();
    const std::filesystem::path sourceFolder = state.DropActionSourceFolder();
    const std::filesystem::path targetFolder = state.DropActionTargetFolder();
    const std::filesystem::path currentFolder = state.SelectedFolder();
    state.CloseDropActionMenu();

    bool executed = false;
    if (action == EditorAssetDropAction::MoveHere) {
        executed = asset.IsValid()
            ? sceneContext.MoveAssetToFolder(asset, targetFolder)
            : sceneContext.MoveAssetFolderToFolder(sourceFolder, targetFolder);
    } else if (action == EditorAssetDropAction::CopyHere) {
        executed = asset.IsValid()
            ? sceneContext.CopyAssetToFolder(asset, targetFolder)
            : sceneContext.CopyAssetFolderToFolder(sourceFolder, targetFolder);
    }

    if (executed) {
        static_cast<void>(state.SelectFolder(currentFolder, sceneContext.Scene().Assets().Manager()));
    }
    return executed;
}

[[nodiscard]] int AssetContentHeight(const EditorAssetBrowserLayoutRects& layout, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const int itemCount = static_cast<int>(state.ChildFolderRows(manager).size() + state.AssetRows(manager).size())
        + (state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1 : 0);
    if (state.ViewMode() == EditorAssetViewMode::Tiles) {
        constexpr int tileGap = 5;
        const int columns = EditorAssetBrowserLayout::AssetTileColumnCount(layout, state.ThumbnailScale());
        const int rows = (itemCount + columns - 1) / std::max(1, columns);
        return rows * (EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) + tileGap);
    }
    return itemCount * EditorAssetBrowserLayout::RowHeight;
}

[[nodiscard]] int AssetViewportHeight(const EditorAssetBrowserLayoutRects& layout, const EditorAssetBrowserState& state) noexcept {
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    int height = static_cast<int>(viewport.bottom - viewport.top);
    if (state.ViewMode() == EditorAssetViewMode::List) {
        height -= EditorAssetBrowserLayout::AssetHeaderHeight;
    }
    return std::max(1, height);
}

[[nodiscard]] bool KeyDown(int virtualKey) noexcept {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

} // namespace

bool EditorAssetBrowserPrimaryClickHandler::HandlePointerDown(
    const RECT& content,
    const EditorAssetBrowserHit& hit,
    int x,
    int y,
    EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(content, state.TreeWidth());

    switch (hit.kind) {
    case EditorAssetBrowserHitKind::Search:
        state.CancelTextEdit();
        state.FocusSearch(true);
        return true;
    case EditorAssetBrowserHitKind::Import: {
        PrepareBrowserAction(state);
        const std::vector<std::filesystem::path> files = EditorAssetImportDialog::Open(GetActiveWindow());
        return files.empty() ? true : sceneContext.ImportAssetFiles(files);
    }
    case EditorAssetBrowserHitKind::Filters:
        PrepareBrowserAction(state);
        state.ToggleFilterMenu();
        return true;
    case EditorAssetBrowserHitKind::FilterFolder:
        PrepareBrowserAction(state);
        state.ToggleShowFolders();
        return true;
    case EditorAssetBrowserHitKind::FilterTemplate:
        PrepareBrowserAction(state);
        state.ToggleShowTemplates();
        return true;
    case EditorAssetBrowserHitKind::Breadcrumb:
        PrepareBrowserAction(state);
        if (const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitTester::BreadcrumbFolderAt(content, x, y, state)) {
            return state.SelectFolder(*folder, manager);
        }
        return true;
    case EditorAssetBrowserHitKind::Refresh:
        PrepareBrowserAction(state);
        static_cast<void>(sceneContext.Scene().Assets().Discover());
        return true;
    case EditorAssetBrowserHitKind::NewFolder:
        return sceneContext.BeginAssetFolderCreation();
    case EditorAssetBrowserHitKind::Rename:
        return sceneContext.BeginAssetRename();
    case EditorAssetBrowserHitKind::DeleteAsset:
        PrepareBrowserAction(state);
        return state.OpenDeleteConfirm();
    case EditorAssetBrowserHitKind::ListMode:
        PrepareBrowserAction(state);
        state.SetViewMode(EditorAssetViewMode::List);
        return true;
    case EditorAssetBrowserHitKind::TileMode:
        PrepareBrowserAction(state);
        state.SetViewMode(EditorAssetViewMode::Tiles);
        return true;
    case EditorAssetBrowserHitKind::Recursive:
        PrepareBrowserAction(state);
        state.ToggleRecursive();
        return true;
    case EditorAssetBrowserHitKind::Sort:
        PrepareBrowserAction(state);
        state.ToggleSortMenu();
        return true;
    case EditorAssetBrowserHitKind::SortByName:
        state.FocusSearch(false);
        state.SetSortMode(EditorAssetSortMode::Name);
        return true;
    case EditorAssetBrowserHitKind::SortByType:
        state.FocusSearch(false);
        state.SetSortMode(EditorAssetSortMode::Type);
        return true;
    case EditorAssetBrowserHitKind::SortByPath:
        state.FocusSearch(false);
        state.SetSortMode(EditorAssetSortMode::Path);
        return true;
    case EditorAssetBrowserHitKind::Slider:
        PrepareBrowserAction(state);
        state.SetThumbnailScale(hit.value);
        state.BeginThumbnailScaleDrag();
        return true;
    case EditorAssetBrowserHitKind::TreeSplitter:
        PrepareBrowserAction(state);
        state.SetTreeWidth(x - content.left);
        state.BeginTreeWidthDrag();
        return true;
    case EditorAssetBrowserHitKind::TreeScrollbarThumb:
        PrepareBrowserAction(state);
        state.BeginTreeScrollbarDrag(y);
        return true;
    case EditorAssetBrowserHitKind::TreeScrollbarTrack: {
        PrepareBrowserAction(state);
        const int contentHeight = static_cast<int>(state.FolderRows(manager).size()) * EditorAssetBrowserLayout::RowHeight;
        const RECT viewport = EditorAssetBrowserLayout::TreeViewportRect(layout);
        const int viewportHeight = static_cast<int>(viewport.bottom - viewport.top);
        const int maxOffset = std::max(0, contentHeight - viewportHeight);
        const RECT track = EditorAssetBrowserLayout::TreeScrollbarTrackRect(layout);
        const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, state.TreeScrollOffset());
        const int page = std::max(1, viewportHeight - EditorAssetBrowserLayout::RowHeight);
        state.SetTreeScrollOffset(state.TreeScrollOffset() + (y < thumb.top ? -page : page), maxOffset);
        return true;
    }
    case EditorAssetBrowserHitKind::ContentScrollbarThumb:
        PrepareBrowserAction(state);
        state.BeginContentScrollbarDrag(y);
        return true;
    case EditorAssetBrowserHitKind::ContentScrollbarTrack: {
        PrepareBrowserAction(state);
        const int contentHeight = AssetContentHeight(layout, state, manager);
        const int viewportHeight = AssetViewportHeight(layout, state);
        const int maxOffset = std::max(0, contentHeight - viewportHeight);
        RECT track = EditorAssetBrowserLayout::AssetScrollbarTrackRect(layout);
        if (state.ViewMode() == EditorAssetViewMode::List) {
            track.top += EditorAssetBrowserLayout::AssetHeaderHeight;
        }
        const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, state.ContentScrollOffset());
        const int page = std::max(1, viewportHeight - EditorAssetBrowserLayout::RowHeight);
        state.SetContentScrollOffset(state.ContentScrollOffset() + (y < thumb.top ? -page : page), maxOffset);
        return true;
    }
    case EditorAssetBrowserHitKind::FolderDisclosure: {
        PrepareBrowserAction(state);
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderDropTargetAt(hit, state, manager);
        return folder.has_value() ? state.ToggleFolderExpanded(*folder, manager) : false;
    }
    case EditorAssetBrowserHitKind::Folder: {
        PrepareBrowserAction(state);
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderAt(hit, state, manager);
        return folder.has_value() ? state.SelectFolder(*folder, manager) : false;
    }
    case EditorAssetBrowserHitKind::ContentFolder: {
        PrepareBrowserAction(state);
        return state.SelectContentFolderAt(hit.index, manager, KeyDown(VK_CONTROL), KeyDown(VK_SHIFT));
    }
    case EditorAssetBrowserHitKind::Asset: {
        PrepareBrowserAction(state);
        // Ctrl/Shift multi-select stays on press. A plain click defers selection
        // to pointer-up, so press-and-drag (e.g. dropping a script onto an object)
        // never changes the Inspector; a quick click still previews on release.
        if (KeyDown(VK_CONTROL) || KeyDown(VK_SHIFT)) {
            state.ClearPendingPreviewAsset();
            return state.SelectAssetAt(hit.index, manager, KeyDown(VK_CONTROL), KeyDown(VK_SHIFT));
        }
        const std::optional<kb::assets::AssetMetadata> metadata = EditorAssetBrowserHitPayloadResolver::AssetMetadataAt(hit, state, manager);
        state.SetPendingPreviewAsset(metadata.has_value() ? metadata->id : kb::assets::AssetId{});
        return true;
    }
    case EditorAssetBrowserHitKind::DropTarget:
        PrepareBrowserAction(state);
        state.CloseSortMenu();
        state.CloseFilterMenu();
        state.FocusSelection(false);
        return true;
    case EditorAssetBrowserHitKind::DeleteConfirmBody:
    case EditorAssetBrowserHitKind::DeleteConfirmListBody:
    case EditorAssetBrowserHitKind::DeleteConfirmCheckbox:
    case EditorAssetBrowserHitKind::DeleteConfirmScrollbarThumb:
    case EditorAssetBrowserHitKind::DeleteConfirmScrollbarTrack:
    case EditorAssetBrowserHitKind::DeleteConfirmAccept:
    case EditorAssetBrowserHitKind::DeleteConfirmCancel:
    case EditorAssetBrowserHitKind::DropActionBody:
        return true;
    case EditorAssetBrowserHitKind::DropActionCommand:
        return ExecuteDropAction(sceneContext, hit.dropAction);
    case EditorAssetBrowserHitKind::ContextMenuBody:
    case EditorAssetBrowserHitKind::ContextMenuCommand:
        return true;
    case EditorAssetBrowserHitKind::None:
    default:
        state.FocusSearch(false);
        state.CloseSortMenu();
        state.CloseFilterMenu();
        state.CloseDropActionMenu();
        return false;
    }
}

} // namespace kb::editor

#endif
