#include "assets/EditorAssetBrowserViewState.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {

std::string_view EditorAssetBrowserViewState::SearchQuery() const noexcept {
    return searchQuery_;
}

bool EditorAssetBrowserViewState::IsSearchFocused() const noexcept {
    return searchFocused_;
}

bool EditorAssetBrowserViewState::Recursive() const noexcept {
    return recursive_;
}

EditorAssetViewMode EditorAssetBrowserViewState::ViewMode() const noexcept {
    return viewMode_;
}

EditorAssetSortMode EditorAssetBrowserViewState::SortMode() const noexcept {
    return sortMode_;
}

std::string_view EditorAssetBrowserViewState::TypeFilter() const noexcept {
    return typeFilter_;
}

bool EditorAssetBrowserViewState::ShowFolders() const noexcept {
    return showFolders_;
}

bool EditorAssetBrowserViewState::ShowTemplates() const noexcept {
    return showTemplates_;
}

bool EditorAssetBrowserViewState::IsFilterMenuOpen() const noexcept {
    return filterMenuOpen_;
}

bool EditorAssetBrowserViewState::IsSortMenuOpen() const noexcept {
    return sortMenuOpen_;
}

float EditorAssetBrowserViewState::ThumbnailScale() const noexcept {
    return thumbnailScale_;
}

bool EditorAssetBrowserViewState::IsThumbnailScaleDragging() const noexcept {
    return thumbnailScaleDragging_;
}

void EditorAssetBrowserViewState::FocusSearch(bool focused) noexcept {
    searchFocused_ = focused;
}

void EditorAssetBrowserViewState::SetSearchQuery(std::string query) {
    searchQuery_ = std::move(query);
}

void EditorAssetBrowserViewState::AppendSearchText(wchar_t character) {
    if (character >= 32 && character < 127) {
        searchQuery_.push_back(static_cast<char>(character));
    }
}

void EditorAssetBrowserViewState::BackspaceSearch() {
    if (!searchQuery_.empty()) {
        searchQuery_.pop_back();
    }
}

void EditorAssetBrowserViewState::ClearSearch() {
    searchQuery_.clear();
}

void EditorAssetBrowserViewState::SetRecursive(bool recursive) noexcept {
    recursive_ = recursive;
}

void EditorAssetBrowserViewState::ToggleRecursive() noexcept {
    recursive_ = !recursive_;
}

void EditorAssetBrowserViewState::SetViewMode(EditorAssetViewMode mode) noexcept {
    viewMode_ = mode;
}

void EditorAssetBrowserViewState::SetSortMode(EditorAssetSortMode mode) noexcept {
    sortMode_ = mode;
    sortMenuOpen_ = false;
}

void EditorAssetBrowserViewState::CycleSortMode() noexcept {
    switch (sortMode_) {
    case EditorAssetSortMode::Name:
        sortMode_ = EditorAssetSortMode::Type;
        break;
    case EditorAssetSortMode::Type:
        sortMode_ = EditorAssetSortMode::Path;
        break;
    case EditorAssetSortMode::Path:
        sortMode_ = EditorAssetSortMode::Name;
        break;
    }
}

void EditorAssetBrowserViewState::SetTypeFilter(std::string type) {
    typeFilter_ = std::move(type);
}

void EditorAssetBrowserViewState::ClearTypeFilter() noexcept {
    typeFilter_.clear();
}

void EditorAssetBrowserViewState::ToggleShowFolders() noexcept {
    showFolders_ = !showFolders_;
}

void EditorAssetBrowserViewState::ToggleShowTemplates() noexcept {
    showTemplates_ = !showTemplates_;
}

void EditorAssetBrowserViewState::ToggleFilterMenu() noexcept {
    filterMenuOpen_ = !filterMenuOpen_;
    sortMenuOpen_ = false;
    searchFocused_ = false;
}

void EditorAssetBrowserViewState::CloseFilterMenu() noexcept {
    filterMenuOpen_ = false;
}

void EditorAssetBrowserViewState::ToggleSortMenu() noexcept {
    sortMenuOpen_ = !sortMenuOpen_;
    filterMenuOpen_ = false;
    searchFocused_ = false;
}

void EditorAssetBrowserViewState::CloseSortMenu() noexcept {
    sortMenuOpen_ = false;
}

void EditorAssetBrowserViewState::SetThumbnailScale(float scale) noexcept {
    thumbnailScale_ = std::clamp(scale, 0.65F, 1.75F);
}

void EditorAssetBrowserViewState::BeginThumbnailScaleDrag() noexcept {
    thumbnailScaleDragging_ = true;
    filterMenuOpen_ = false;
    sortMenuOpen_ = false;
}

void EditorAssetBrowserViewState::EndThumbnailScaleDrag() noexcept {
    thumbnailScaleDragging_ = false;
}

} // namespace kb::editor
