#include "assets/EditorAssetBrowserState.hpp"

#include "assets/EditorAssetBrowserPathUtils.hpp"

namespace kb::editor {

std::vector<EditorAssetContextMenuItem> EditorAssetBrowserState::ContextMenuItems(const kb::assets::AssetManager& manager) const {
    const bool assetExists = manager.Registry().Find(contextMenu_.TargetAsset()) != nullptr;
    return contextMenu_.Items(assetExists, ContextMenuTargetFolderCanMutate(manager));
}

void EditorAssetBrowserState::OpenContextMenuForBackground(int x, int y) {
    contextMenu_.OpenForBackground(x, y, selection_.SelectedFolder());
    deleteConfirm_.Close();
    view_.FocusSearch(false);
    view_.CloseSortMenu();
}

bool EditorAssetBrowserState::OpenContextMenuForAsset(int x, int y, kb::assets::AssetId id, const kb::assets::AssetManager& manager) {
    if (manager.Registry().Find(id) == nullptr) {
        return false;
    }

    contextMenu_.OpenForAsset(x, y, id);
    deleteConfirm_.Close();
    view_.FocusSearch(false);
    view_.CloseSortMenu();
    return true;
}

bool EditorAssetBrowserState::OpenContextMenuForFolder(int x, int y, const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    const std::filesystem::path normalized{ asset_browser::Normalize(virtualPath) };
    if (!selection_.FolderExists(normalized, manager)) {
        return false;
    }

    contextMenu_.OpenForFolder(x, y, normalized);
    deleteConfirm_.Close();
    view_.FocusSearch(false);
    view_.CloseSortMenu();
    return true;
}

void EditorAssetBrowserState::CloseContextMenu() noexcept {
    contextMenu_.Close();
}

bool EditorAssetBrowserState::OpenDeleteConfirm() noexcept {
    if (selection_.SelectionKind() != EditorAssetBrowserSelectionKind::Folder || !selection_.IsSelectionFocused()) {
        return false;
    }
    deleteConfirm_.Open();
    view_.FocusSearch(false);
    view_.CloseSortMenu();
    contextMenu_.Close();
    return true;
}

void EditorAssetBrowserState::CloseDeleteConfirm() noexcept {
    deleteConfirm_.Close();
}

void EditorAssetBrowserState::BeginDeleteConfirmDrag(int x, int y) noexcept {
    deleteConfirm_.BeginDrag(x, y);
}

void EditorAssetBrowserState::DragDeleteConfirm(int x, int y) noexcept {
    deleteConfirm_.Drag(x, y);
}

void EditorAssetBrowserState::EndDeleteConfirmDrag() noexcept {
    deleteConfirm_.EndDrag();
}

bool EditorAssetBrowserState::SetContextMenuHoveredCommand(EditorAssetContextCommand command) noexcept {
    return contextMenu_.SetHoveredCommand(command);
}

bool EditorAssetBrowserState::ContextMenuTargetFolderCanMutate(const kb::assets::AssetManager& manager) const {
    return contextMenu_.TargetKind() == EditorAssetContextTargetKind::Folder
        && asset_browser::Normalize(contextMenu_.TargetFolder()) != "/Game"
        && selection_.FolderExists(contextMenu_.TargetFolder(), manager);
}

} // namespace kb::editor
