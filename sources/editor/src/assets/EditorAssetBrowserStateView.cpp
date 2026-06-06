#include "assets/EditorAssetBrowserState.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace kb::editor {

void EditorAssetBrowserState::FocusSearch(bool focused) noexcept {
    view_.FocusSearch(focused);
    if (focused) {
        selection_.FocusSelection(false);
        view_.CloseFilterMenu();
        view_.CloseSortMenu();
        contextMenu_.Close();
    }
}

void EditorAssetBrowserState::FocusSelection(bool focused) noexcept {
    selection_.FocusSelection(focused);
    if (selection_.IsSelectionFocused()) {
        view_.FocusSearch(false);
    }
}

void EditorAssetBrowserState::SetSearchQuery(std::string query) {
    view_.SetSearchQuery(std::move(query));
}

void EditorAssetBrowserState::AppendSearchText(wchar_t character) {
    view_.AppendSearchText(character);
}

void EditorAssetBrowserState::BackspaceSearch() {
    view_.BackspaceSearch();
}

void EditorAssetBrowserState::ClearSearch() {
    view_.ClearSearch();
}

void EditorAssetBrowserState::SetRecursive(bool recursive) noexcept {
    view_.SetRecursive(recursive);
}

void EditorAssetBrowserState::ToggleRecursive() noexcept {
    view_.ToggleRecursive();
}

void EditorAssetBrowserState::SetViewMode(EditorAssetViewMode mode) noexcept {
    view_.SetViewMode(mode);
}

void EditorAssetBrowserState::SetSortMode(EditorAssetSortMode mode) noexcept {
    view_.SetSortMode(mode);
    view_.CloseFilterMenu();
    contextMenu_.Close();
}

void EditorAssetBrowserState::CycleSortMode() noexcept {
    view_.CycleSortMode();
}

void EditorAssetBrowserState::CycleTypeFilter(const kb::assets::AssetManager& manager) {
    const std::vector<std::string> types = AssetTypes(manager);
    if (types.empty()) {
        view_.ClearTypeFilter();
        return;
    }
    if (view_.TypeFilter().empty()) {
        view_.SetTypeFilter(types.front());
        return;
    }

    const auto current = std::ranges::find(types, view_.TypeFilter());
    if (current == types.end() || std::next(current) == types.end()) {
        view_.ClearTypeFilter();
    } else {
        view_.SetTypeFilter(*std::next(current));
    }
}

void EditorAssetBrowserState::ToggleFilterMenu() noexcept {
    view_.ToggleFilterMenu();
    filterMenuHoveredIndex_ = -1;
    contextMenu_.Close();
    CloseDropActionMenu();
}

void EditorAssetBrowserState::CloseFilterMenu() noexcept {
    view_.CloseFilterMenu();
    filterMenuHoveredIndex_ = -1;
}

void EditorAssetBrowserState::ToggleShowFolders() noexcept {
    view_.ToggleShowFolders();
}

void EditorAssetBrowserState::ToggleShowTemplates() noexcept {
    view_.ToggleShowTemplates();
}

bool EditorAssetBrowserState::SetFilterMenuHoveredIndex(int index) noexcept {
    if (filterMenuHoveredIndex_ == index) {
        return false;
    }
    filterMenuHoveredIndex_ = index;
    return true;
}

void EditorAssetBrowserState::ToggleSortMenu() noexcept {
    view_.ToggleSortMenu();
    contextMenu_.Close();
    CloseDropActionMenu();
}

void EditorAssetBrowserState::CloseSortMenu() noexcept {
    view_.CloseSortMenu();
}

void EditorAssetBrowserState::SetThumbnailScale(float scale) noexcept {
    view_.SetThumbnailScale(scale);
}

void EditorAssetBrowserState::BeginThumbnailScaleDrag() noexcept {
    view_.BeginThumbnailScaleDrag();
    contextMenu_.Close();
}

void EditorAssetBrowserState::EndThumbnailScaleDrag() noexcept {
    view_.EndThumbnailScaleDrag();
}

} // namespace kb::editor
