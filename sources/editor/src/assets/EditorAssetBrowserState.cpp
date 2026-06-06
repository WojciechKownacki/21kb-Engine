#include "assets/EditorAssetBrowserState.hpp"

namespace kb::editor {

const std::filesystem::path& EditorAssetBrowserState::SelectedFolder() const noexcept {
    return selection_.SelectedFolder();
}

const std::filesystem::path& EditorAssetBrowserState::SelectedContentFolder() const noexcept {
    return selection_.SelectedContentFolder();
}

kb::assets::AssetId EditorAssetBrowserState::SelectedAsset() const noexcept {
    return selection_.SelectedAsset();
}

EditorAssetBrowserSelectionKind EditorAssetBrowserState::SelectionKind() const noexcept {
    return selection_.SelectionKind();
}

std::string_view EditorAssetBrowserState::SearchQuery() const noexcept {
    return view_.SearchQuery();
}

bool EditorAssetBrowserState::IsSearchFocused() const noexcept {
    return view_.IsSearchFocused();
}

bool EditorAssetBrowserState::IsSelectionFocused() const noexcept {
    return selection_.IsSelectionFocused();
}

bool EditorAssetBrowserState::Recursive() const noexcept {
    return view_.Recursive();
}

EditorAssetViewMode EditorAssetBrowserState::ViewMode() const noexcept {
    return view_.ViewMode();
}

EditorAssetSortMode EditorAssetBrowserState::SortMode() const noexcept {
    return view_.SortMode();
}

std::string_view EditorAssetBrowserState::TypeFilter() const noexcept {
    return view_.TypeFilter();
}

bool EditorAssetBrowserState::IsSortMenuOpen() const noexcept {
    return view_.IsSortMenuOpen();
}

float EditorAssetBrowserState::ThumbnailScale() const noexcept {
    return view_.ThumbnailScale();
}

bool EditorAssetBrowserState::IsThumbnailScaleDragging() const noexcept {
    return view_.IsThumbnailScaleDragging();
}

EditorAssetTextEditMode EditorAssetBrowserState::TextEditMode() const noexcept {
    return textEdit_.Mode();
}

kb::assets::AssetId EditorAssetBrowserState::TextEditTargetAsset() const noexcept {
    return textEdit_.TargetAsset();
}

const std::filesystem::path& EditorAssetBrowserState::TextEditTargetFolder() const noexcept {
    return textEdit_.TargetFolder();
}

std::string_view EditorAssetBrowserState::TextEditValue() const noexcept {
    return textEdit_.Value();
}

bool EditorAssetBrowserState::IsTextEditing() const noexcept {
    return textEdit_.IsEditing();
}

bool EditorAssetBrowserState::IsContextMenuOpen() const noexcept {
    return contextMenu_.IsOpen();
}

bool EditorAssetBrowserState::IsDeleteConfirmOpen() const noexcept {
    return deleteConfirm_.IsOpen();
}

bool EditorAssetBrowserState::IsDeleteConfirmDragging() const noexcept {
    return deleteConfirm_.IsDragging();
}

int EditorAssetBrowserState::DeleteConfirmOffsetX() const noexcept {
    return deleteConfirm_.OffsetX();
}

int EditorAssetBrowserState::DeleteConfirmOffsetY() const noexcept {
    return deleteConfirm_.OffsetY();
}

int EditorAssetBrowserState::ContextMenuX() const noexcept {
    return contextMenu_.X();
}

int EditorAssetBrowserState::ContextMenuY() const noexcept {
    return contextMenu_.Y();
}

EditorAssetContextTargetKind EditorAssetBrowserState::ContextMenuTargetKind() const noexcept {
    return contextMenu_.TargetKind();
}

kb::assets::AssetId EditorAssetBrowserState::ContextMenuTargetAsset() const noexcept {
    return contextMenu_.TargetAsset();
}

const std::filesystem::path& EditorAssetBrowserState::ContextMenuTargetFolder() const noexcept {
    return contextMenu_.TargetFolder();
}

EditorAssetContextCommand EditorAssetBrowserState::ContextMenuHoveredCommand() const noexcept {
    return contextMenu_.HoveredCommand();
}

bool EditorAssetBrowserState::IsDropActionMenuOpen() const noexcept {
    return dropActionMenuOpen_;
}

int EditorAssetBrowserState::DropActionMenuX() const noexcept {
    return dropActionMenuX_;
}

int EditorAssetBrowserState::DropActionMenuY() const noexcept {
    return dropActionMenuY_;
}

const std::filesystem::path& EditorAssetBrowserState::DropActionTargetFolder() const noexcept {
    return dropActionTargetFolder_;
}

kb::assets::AssetId EditorAssetBrowserState::DropActionAsset() const noexcept {
    return dropActionAsset_;
}

const std::filesystem::path& EditorAssetBrowserState::DropActionSourceFolder() const noexcept {
    return dropActionSourceFolder_;
}

EditorAssetDropAction EditorAssetBrowserState::DropActionHoveredCommand() const noexcept {
    return dropActionHovered_;
}

const kb::assets::AssetMetadata* EditorAssetBrowserState::SelectedMetadata(const kb::assets::AssetManager& manager) const noexcept {
    return manager.Registry().Find(selection_.SelectedAsset());
}

} // namespace kb::editor
