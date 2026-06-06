#pragma once

#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <string>
#include <string_view>

namespace kb::editor {

class EditorAssetBrowserViewState {
public:
    [[nodiscard]] std::string_view SearchQuery() const noexcept;
    [[nodiscard]] bool IsSearchFocused() const noexcept;
    [[nodiscard]] bool Recursive() const noexcept;
    [[nodiscard]] EditorAssetViewMode ViewMode() const noexcept;
    [[nodiscard]] EditorAssetSortMode SortMode() const noexcept;
    [[nodiscard]] std::string_view TypeFilter() const noexcept;
    [[nodiscard]] bool ShowFolders() const noexcept;
    [[nodiscard]] bool ShowTemplates() const noexcept;
    [[nodiscard]] bool IsFilterMenuOpen() const noexcept;
    [[nodiscard]] bool IsSortMenuOpen() const noexcept;
    [[nodiscard]] float ThumbnailScale() const noexcept;
    [[nodiscard]] bool IsThumbnailScaleDragging() const noexcept;

    void FocusSearch(bool focused) noexcept;
    void SetSearchQuery(std::string query);
    void AppendSearchText(wchar_t character);
    void BackspaceSearch();
    void ClearSearch();
    void SetRecursive(bool recursive) noexcept;
    void ToggleRecursive() noexcept;
    void SetViewMode(EditorAssetViewMode mode) noexcept;
    void SetSortMode(EditorAssetSortMode mode) noexcept;
    void CycleSortMode() noexcept;
    void SetTypeFilter(std::string type);
    void ClearTypeFilter() noexcept;
    void ToggleShowFolders() noexcept;
    void ToggleShowTemplates() noexcept;
    void ToggleFilterMenu() noexcept;
    void CloseFilterMenu() noexcept;
    void ToggleSortMenu() noexcept;
    void CloseSortMenu() noexcept;
    void SetThumbnailScale(float scale) noexcept;
    void BeginThumbnailScaleDrag() noexcept;
    void EndThumbnailScaleDrag() noexcept;

private:
    std::string searchQuery_;
    std::string typeFilter_;
    bool searchFocused_ = false;
    bool recursive_ = false;
    bool showFolders_ = true;
    bool showTemplates_ = true;
    bool filterMenuOpen_ = false;
    bool sortMenuOpen_ = false;
    bool thumbnailScaleDragging_ = false;
    float thumbnailScale_ = 1.0F;
    EditorAssetViewMode viewMode_ = EditorAssetViewMode::Tiles;
    EditorAssetSortMode sortMode_ = EditorAssetSortMode::Name;
};

} // namespace kb::editor
