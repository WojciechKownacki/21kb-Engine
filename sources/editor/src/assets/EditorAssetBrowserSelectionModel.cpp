#include "assets/EditorAssetBrowserSelectionModel.hpp"

namespace kb::editor {

const std::filesystem::path& EditorAssetBrowserSelectionModel::SelectedFolder() const noexcept {
    return selectedFolder_;
}

const std::filesystem::path& EditorAssetBrowserSelectionModel::SelectedContentFolder() const noexcept {
    return selectedContentFolder_;
}

kb::assets::AssetId EditorAssetBrowserSelectionModel::SelectedAsset() const noexcept {
    return selectedAsset_;
}

EditorAssetBrowserSelectionKind EditorAssetBrowserSelectionModel::SelectionKind() const noexcept {
    return selectionKind_;
}

bool EditorAssetBrowserSelectionModel::IsSelectionFocused() const noexcept {
    return selectionFocused_;
}

void EditorAssetBrowserSelectionModel::FocusSelection(bool focused) noexcept {
    selectionFocused_ = focused && selectionKind_ != EditorAssetBrowserSelectionKind::None;
}

void EditorAssetBrowserSelectionModel::SelectFolder(std::filesystem::path virtualPath) {
    selectedFolder_ = std::move(virtualPath);
    selectedAsset_ = {};
    selectedContentFolder_.clear();
    selectionKind_ = EditorAssetBrowserSelectionKind::Folder;
    selectionFocused_ = true;
}

void EditorAssetBrowserSelectionModel::SelectContentFolder(std::filesystem::path virtualPath) {
    selectedContentFolder_ = std::move(virtualPath);
    selectedAsset_ = {};
    selectionKind_ = EditorAssetBrowserSelectionKind::Folder;
    selectionFocused_ = true;
}

void EditorAssetBrowserSelectionModel::SelectAsset(kb::assets::AssetId id, std::filesystem::path folder) {
    selectedAsset_ = id;
    selectedContentFolder_.clear();
    selectionKind_ = EditorAssetBrowserSelectionKind::Asset;
    selectionFocused_ = true;
    selectedFolder_ = std::move(folder);
}

void EditorAssetBrowserSelectionModel::ClearSelection() noexcept {
    selectedAsset_ = {};
    selectedContentFolder_.clear();
    selectionKind_ = EditorAssetBrowserSelectionKind::None;
    selectionFocused_ = false;
}

} // namespace kb::editor
