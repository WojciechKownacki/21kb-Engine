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
    CloseDropActionMenu();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
}

bool EditorAssetBrowserState::OpenContextMenuForAsset(int x, int y, kb::assets::AssetId id, const kb::assets::AssetManager& manager) {
    if (manager.Registry().Find(id) == nullptr) {
        return false;
    }

    contextMenu_.OpenForAsset(x, y, id);
    deleteConfirm_.Close();
    CloseDropActionMenu();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
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
    CloseDropActionMenu();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
    return true;
}

void EditorAssetBrowserState::CloseContextMenu() noexcept {
    contextMenu_.Close();
}

void EditorAssetBrowserState::OpenDropActionMenuForAsset(int x, int y, kb::assets::AssetId id, const std::filesystem::path& targetFolder) {
    dropActionMenuOpen_ = true;
    dropActionMenuX_ = x;
    dropActionMenuY_ = y;
    dropActionAsset_ = id;
    dropActionSourceFolder_.clear();
    dropActionTargetFolder_ = targetFolder;
    dropActionHovered_ = EditorAssetDropAction::None;
    contextMenu_.Close();
    deleteConfirm_.Close();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
}

void EditorAssetBrowserState::OpenDropActionMenuForFolder(int x, int y, const std::filesystem::path& sourceFolder, const std::filesystem::path& targetFolder) {
    dropActionMenuOpen_ = true;
    dropActionMenuX_ = x;
    dropActionMenuY_ = y;
    dropActionAsset_ = {};
    dropActionSourceFolder_ = sourceFolder;
    dropActionTargetFolder_ = targetFolder;
    dropActionHovered_ = EditorAssetDropAction::None;
    contextMenu_.Close();
    deleteConfirm_.Close();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
}

void EditorAssetBrowserState::CloseDropActionMenu() noexcept {
    dropActionMenuOpen_ = false;
    dropActionHovered_ = EditorAssetDropAction::None;
}

bool EditorAssetBrowserState::OpenDeleteConfirm() noexcept {
    if (selection_.SelectionKind() != EditorAssetBrowserSelectionKind::Folder || !selection_.IsSelectionFocused()) {
        return false;
    }
    deleteConfirm_.Open();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
    contextMenu_.Close();
    CloseDropActionMenu();
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

bool EditorAssetBrowserState::SetDropActionHoveredCommand(EditorAssetDropAction command) noexcept {
    if (dropActionHovered_ == command) {
        return false;
    }
    dropActionHovered_ = command;
    return true;
}

bool EditorAssetBrowserState::ContextMenuTargetFolderCanMutate(const kb::assets::AssetManager& manager) const {
    return contextMenu_.TargetKind() == EditorAssetContextTargetKind::Folder
        && asset_browser::Normalize(contextMenu_.TargetFolder()) != "/Game"
        && selection_.FolderExists(contextMenu_.TargetFolder(), manager);
}

} // namespace kb::editor
