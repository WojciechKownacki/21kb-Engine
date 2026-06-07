#include "assets/EditorAssetBrowserSelectionModel.hpp"

#include "assets/EditorAssetBrowserPathUtils.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] std::string NormalizePath(const std::filesystem::path& path) {
    return asset_browser::Normalize(path);
}

} // namespace

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

bool EditorAssetBrowserSelectionModel::IsContentFolderSelected(const std::filesystem::path& virtualPath) const {
    return selectedContentFolders_.contains(NormalizePath(virtualPath));
}

bool EditorAssetBrowserSelectionModel::IsAssetSelected(kb::assets::AssetId id) const noexcept {
    return id.IsValid() && selectedAssets_.contains(id.value);
}

void EditorAssetBrowserSelectionModel::FocusSelection(bool focused) noexcept {
    selectionFocused_ = focused && selectionKind_ != EditorAssetBrowserSelectionKind::None;
}

void EditorAssetBrowserSelectionModel::SelectFolder(std::filesystem::path virtualPath) {
    selectedFolder_ = std::move(virtualPath);
    ClearSelectedContentItems();
    selectionKind_ = EditorAssetBrowserSelectionKind::Folder;
    selectionFocused_ = true;
}

void EditorAssetBrowserSelectionModel::SelectContentFolder(std::filesystem::path virtualPath) {
    ClearSelectedContentItems();
    AddContentFolder(std::move(virtualPath));
}

void EditorAssetBrowserSelectionModel::AddContentFolder(std::filesystem::path virtualPath) {
    const std::string normalized = NormalizePath(virtualPath);
    selectedContentFolder_ = std::move(virtualPath);
    selectedContentFolders_.insert(normalized);
    selectedAsset_ = {};
    selectionKind_ = EditorAssetBrowserSelectionKind::Folder;
    selectionFocused_ = true;
}

void EditorAssetBrowserSelectionModel::ToggleContentFolder(std::filesystem::path virtualPath) {
    const std::string normalized = NormalizePath(virtualPath);
    if (selectedContentFolders_.erase(normalized) > 0U) {
        if (NormalizePath(selectedContentFolder_) == normalized) {
            selectedContentFolder_.clear();
        }
        NormalizePrimaryAfterToggle();
        return;
    }

    selectedContentFolders_.insert(normalized);
    selectedContentFolder_ = std::move(virtualPath);
    selectedAsset_ = {};
    selectionKind_ = EditorAssetBrowserSelectionKind::Folder;
    selectionFocused_ = true;
}

void EditorAssetBrowserSelectionModel::SelectAsset(kb::assets::AssetId id, std::filesystem::path folder) {
    ClearSelectedContentItems();
    AddAsset(id, std::move(folder));
}

void EditorAssetBrowserSelectionModel::AddAsset(kb::assets::AssetId id, std::filesystem::path folder) {
    if (!id.IsValid()) {
        return;
    }
    selectedAsset_ = id;
    selectedAssets_.insert(id.value);
    selectedContentFolder_.clear();
    selectionKind_ = EditorAssetBrowserSelectionKind::Asset;
    selectionFocused_ = true;
    selectedFolder_ = std::move(folder);
}

void EditorAssetBrowserSelectionModel::ToggleAsset(kb::assets::AssetId id, std::filesystem::path folder) {
    if (!id.IsValid()) {
        return;
    }
    if (selectedAssets_.erase(id.value) > 0U) {
        if (selectedAsset_ == id) {
            selectedAsset_ = {};
        }
        NormalizePrimaryAfterToggle();
        return;
    }

    selectedAssets_.insert(id.value);
    selectedAsset_ = id;
    selectedContentFolder_.clear();
    selectionKind_ = EditorAssetBrowserSelectionKind::Asset;
    selectionFocused_ = true;
    selectedFolder_ = std::move(folder);
}

void EditorAssetBrowserSelectionModel::ClearContentSelection() noexcept {
    ClearSelectedContentItems();
    selectionKind_ = EditorAssetBrowserSelectionKind::None;
    selectionFocused_ = false;
}

void EditorAssetBrowserSelectionModel::ClearSelection() noexcept {
    ClearSelectedContentItems();
    selectionKind_ = EditorAssetBrowserSelectionKind::None;
    selectionFocused_ = false;
}

void EditorAssetBrowserSelectionModel::ClearSelectedContentItems() noexcept {
    selectedAsset_ = {};
    selectedContentFolder_.clear();
    selectedContentFolders_.clear();
    selectedAssets_.clear();
}

void EditorAssetBrowserSelectionModel::NormalizePrimaryAfterToggle() noexcept {
    if (!selectedAssets_.empty()) {
        selectedAsset_ = kb::assets::AssetId{ *selectedAssets_.begin() };
        selectedContentFolder_.clear();
        selectionKind_ = EditorAssetBrowserSelectionKind::Asset;
        selectionFocused_ = true;
        return;
    }

    if (!selectedContentFolders_.empty()) {
        selectedContentFolder_ = std::filesystem::path{ *selectedContentFolders_.begin() };
        selectedAsset_ = {};
        selectionKind_ = EditorAssetBrowserSelectionKind::Folder;
        selectionFocused_ = true;
        return;
    }

    selectedAsset_ = {};
    selectedContentFolder_.clear();
    selectionKind_ = EditorAssetBrowserSelectionKind::None;
    selectionFocused_ = false;
}

} // namespace kb::editor
