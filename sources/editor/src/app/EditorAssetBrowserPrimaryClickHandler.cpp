#include "app/EditorAssetBrowserPrimaryClickHandler.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/EditorSceneContext.hpp"

#include <filesystem>
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

} // namespace

bool EditorAssetBrowserPrimaryClickHandler::HandlePointerDown(
    const RECT& content,
    const EditorAssetBrowserHit& hit,
    int x,
    int y,
    EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();

    switch (hit.kind) {
    case EditorAssetBrowserHitKind::Search:
        state.CancelTextEdit();
        state.FocusSearch(true);
        return true;
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
        const std::optional<std::filesystem::path> folder = EditorAssetBrowserHitPayloadResolver::FolderAt(hit, state, manager);
        return folder.has_value() ? state.SelectContentFolder(*folder, manager) : false;
    }
    case EditorAssetBrowserHitKind::Asset: {
        PrepareBrowserAction(state);
        const std::optional<kb::assets::AssetId> asset = EditorAssetBrowserHitPayloadResolver::AssetIdAt(hit, state, manager);
        return asset.has_value() ? state.SelectAsset(*asset, manager) : false;
    }
    case EditorAssetBrowserHitKind::DropTarget:
        PrepareBrowserAction(state);
        state.CloseSortMenu();
        state.CloseFilterMenu();
        state.FocusSelection(false);
        return true;
    case EditorAssetBrowserHitKind::DeleteConfirmBody:
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
