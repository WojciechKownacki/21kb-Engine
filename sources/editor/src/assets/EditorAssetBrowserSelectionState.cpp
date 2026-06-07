#include "assets/EditorAssetBrowserSelectionState.hpp"

#include "assets/EditorAssetBrowserFolderRowBuilder.hpp"
#include "assets/EditorAssetBrowserPathUtils.hpp"

namespace kb::editor {

const std::filesystem::path& EditorAssetBrowserSelectionState::SelectedFolder() const noexcept {
    return selection_.SelectedFolder();
}

const std::filesystem::path& EditorAssetBrowserSelectionState::SelectedContentFolder() const noexcept {
    return selection_.SelectedContentFolder();
}

kb::assets::AssetId EditorAssetBrowserSelectionState::SelectedAsset() const noexcept {
    return selection_.SelectedAsset();
}

EditorAssetBrowserSelectionKind EditorAssetBrowserSelectionState::SelectionKind() const noexcept {
    return selection_.SelectionKind();
}

bool EditorAssetBrowserSelectionState::IsSelectionFocused() const noexcept {
    return selection_.IsSelectionFocused();
}

bool EditorAssetBrowserSelectionState::IsContentFolderSelected(const std::filesystem::path& virtualPath) const {
    return selection_.IsContentFolderSelected(virtualPath);
}

bool EditorAssetBrowserSelectionState::IsAssetSelected(kb::assets::AssetId id) const noexcept {
    return selection_.IsAssetSelected(id);
}

void EditorAssetBrowserSelectionState::FocusSelection(bool focused) noexcept {
    selection_.FocusSelection(focused);
}

bool EditorAssetBrowserSelectionState::SelectFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    const std::filesystem::path normalized{ asset_browser::Normalize(virtualPath) };
    if (!FolderExists(normalized, manager)) {
        return false;
    }

    selection_.SelectFolder(normalized);
    expansion_.ExpandAncestors(normalized);
    return true;
}

bool EditorAssetBrowserSelectionState::SelectContentFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    const std::filesystem::path normalized{ asset_browser::Normalize(virtualPath) };
    if (!FolderExists(normalized, manager)) {
        return false;
    }

    selection_.SelectContentFolder(normalized);
    return true;
}

bool EditorAssetBrowserSelectionState::AddContentFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    const std::filesystem::path normalized{ asset_browser::Normalize(virtualPath) };
    if (!FolderExists(normalized, manager)) {
        return false;
    }

    selection_.AddContentFolder(normalized);
    return true;
}

bool EditorAssetBrowserSelectionState::ToggleContentFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    const std::filesystem::path normalized{ asset_browser::Normalize(virtualPath) };
    if (!FolderExists(normalized, manager)) {
        return false;
    }

    selection_.ToggleContentFolder(normalized);
    return true;
}

bool EditorAssetBrowserSelectionState::SelectAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager) {
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr) {
        ClearSelection();
        return false;
    }

    const std::filesystem::path folder = asset_browser::ParentVirtualPath(metadata->virtualPath);
    selection_.SelectAsset(id, folder);
    expansion_.ExpandAncestors(folder);
    return true;
}

bool EditorAssetBrowserSelectionState::AddAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager) {
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr) {
        return false;
    }

    const std::filesystem::path folder = asset_browser::ParentVirtualPath(metadata->virtualPath);
    selection_.AddAsset(id, folder);
    expansion_.ExpandAncestors(folder);
    return true;
}

bool EditorAssetBrowserSelectionState::ToggleAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager) {
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr) {
        return false;
    }

    const std::filesystem::path folder = asset_browser::ParentVirtualPath(metadata->virtualPath);
    selection_.ToggleAsset(id, folder);
    expansion_.ExpandAncestors(folder);
    return true;
}

void EditorAssetBrowserSelectionState::ClearContentSelection() noexcept {
    selection_.ClearContentSelection();
}

bool EditorAssetBrowserSelectionState::ToggleFolderExpanded(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    return expansion_.ToggleFolderExpanded(virtualPath, manager);
}

void EditorAssetBrowserSelectionState::ClearSelection() noexcept {
    selection_.ClearSelection();
}

bool EditorAssetBrowserSelectionState::FolderExists(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) const {
    return asset_browser::FolderExists(virtualPath, manager);
}

std::vector<EditorAssetFolderRow> EditorAssetBrowserSelectionState::FolderRows(const kb::assets::AssetManager& manager) const {
    return EditorAssetBrowserFolderRowBuilder::BuildTreeRows(manager, selection_.SelectedFolder(), expansion_.CollapsedFolders());
}

std::vector<EditorAssetFolderRow> EditorAssetBrowserSelectionState::ChildFolderRows(const kb::assets::AssetManager& manager) const {
    return EditorAssetBrowserFolderRowBuilder::BuildChildRows(manager, selection_.SelectedFolder(), selection_.SelectedContentFolder());
}

} // namespace kb::editor
