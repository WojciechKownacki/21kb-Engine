#include "assets/EditorAssetBrowserState.hpp"

#include "assets/EditorAssetBrowserPathUtils.hpp"

#include <utility>

namespace kb::editor {

void EditorAssetBrowserState::BeginNewFolder() {
    textEdit_.BeginNewFolder(selection_.SelectedFolder());
    view_.FocusSearch(false);
    view_.CloseSortMenu();
    contextMenu_.Close();
    selection_.ClearSelection();
    deleteConfirm_.Close();
}

bool EditorAssetBrowserState::BeginRenameSelection(const kb::assets::AssetManager& manager) {
    if (selection_.SelectedAsset().IsValid()) {
        return BeginRenameAsset(selection_.SelectedAsset(), manager);
    }
    if (!selection_.SelectedContentFolder().empty()) {
        return BeginRenameFolder(selection_.SelectedContentFolder(), manager);
    }
    return BeginRenameFolder(selection_.SelectedFolder(), manager);
}

bool EditorAssetBrowserState::BeginRenameAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager) {
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr) {
        return false;
    }

    if (!selection_.SelectAsset(id, manager)) {
        return false;
    }

    textEdit_.BeginRenameAsset(id, metadata->name);
    view_.FocusSearch(false);
    view_.CloseSortMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    return true;
}

bool EditorAssetBrowserState::BeginRenameFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    const std::filesystem::path normalized{ asset_browser::Normalize(virtualPath) };
    if (asset_browser::Normalize(normalized) == "/Game" || !selection_.FolderExists(normalized, manager)) {
        return false;
    }

    if (!selection_.SelectContentFolder(normalized, manager)) {
        return false;
    }

    textEdit_.BeginRenameFolder(normalized, normalized.filename().string());
    view_.FocusSearch(false);
    view_.CloseSortMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    return true;
}

void EditorAssetBrowserState::SetTextEditValue(std::string value) {
    textEdit_.SetValue(std::move(value));
}

void EditorAssetBrowserState::AppendTextEdit(wchar_t character) {
    textEdit_.Append(character);
}

void EditorAssetBrowserState::BackspaceTextEdit() {
    textEdit_.Backspace();
}

void EditorAssetBrowserState::CancelTextEdit() noexcept {
    textEdit_.Cancel();
}

} // namespace kb::editor
