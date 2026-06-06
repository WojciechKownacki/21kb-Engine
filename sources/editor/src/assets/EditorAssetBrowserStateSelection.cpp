#include "assets/EditorAssetBrowserState.hpp"

namespace kb::editor {

bool EditorAssetBrowserState::SelectFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    if (!selection_.SelectFolder(virtualPath, manager)) {
        return false;
    }
    view_.CloseSortMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    CloseDropActionMenu();
    return true;
}

bool EditorAssetBrowserState::SelectContentFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    if (!selection_.SelectContentFolder(virtualPath, manager)) {
        return false;
    }
    view_.CloseSortMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    CloseDropActionMenu();
    return true;
}

bool EditorAssetBrowserState::SelectAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager) {
    if (!selection_.SelectAsset(id, manager)) {
        return false;
    }
    view_.CloseSortMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    CloseDropActionMenu();
    return true;
}

bool EditorAssetBrowserState::ToggleFolderExpanded(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    if (!selection_.ToggleFolderExpanded(virtualPath, manager)) {
        return false;
    }
    view_.CloseSortMenu();
    contextMenu_.Close();
    CloseDropActionMenu();
    return true;
}

void EditorAssetBrowserState::ClearSelection() noexcept {
    selection_.ClearSelection();
    deleteConfirm_.Close();
    CloseDropActionMenu();
}

} // namespace kb::editor
