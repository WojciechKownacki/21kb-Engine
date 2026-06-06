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
    [[nodiscard]] int TreeWidth() const noexcept;
    [[nodiscard]] bool IsTreeWidthDragging() const noexcept;
    [[nodiscard]] int TreeScrollOffset() const noexcept;
    [[nodiscard]] bool IsTreeScrollbarDragging() const noexcept;
    [[nodiscard]] int ContentScrollOffset() const noexcept;
    [[nodiscard]] bool IsContentScrollbarDragging() const noexcept;

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
    void SetTreeWidth(int width) noexcept;
    void BeginTreeWidthDrag() noexcept;
    void EndTreeWidthDrag() noexcept;
    void SetTreeScrollOffset(int offset, int maxOffset) noexcept;
    void BeginTreeScrollbarDrag(int y) noexcept;
    void DragTreeScrollbar(int y, int trackTravel, int maxOffset) noexcept;
    void EndTreeScrollbarDrag() noexcept;
    void SetContentScrollOffset(int offset, int maxOffset) noexcept;
    void BeginContentScrollbarDrag(int y) noexcept;
    void DragContentScrollbar(int y, int trackTravel, int maxOffset) noexcept;
    void EndContentScrollbarDrag() noexcept;

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
    bool treeWidthDragging_ = false;
    bool treeScrollbarDragging_ = false;
    bool contentScrollbarDragging_ = false;
    int treeWidth_ = 0;
    int treeScrollOffset_ = 0;
    int treeScrollbarDragY_ = 0;
    int treeScrollbarDragStartOffset_ = 0;
    int contentScrollOffset_ = 0;
    int contentScrollbarDragY_ = 0;
    int contentScrollbarDragStartOffset_ = 0;
    float thumbnailScale_ = 1.0F;
    EditorAssetViewMode viewMode_ = EditorAssetViewMode::Tiles;
    EditorAssetSortMode sortMode_ = EditorAssetSortMode::Name;
};

} // namespace kb::editor
