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

void EditorAssetBrowserState::InsertSearchText(std::string_view text) {
    view_.InsertSearchText(text);
}

void EditorAssetBrowserState::BackspaceSearch() {
    view_.BackspaceSearch();
}

void EditorAssetBrowserState::SelectAllSearch() noexcept {
    view_.SelectAllSearch();
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

void EditorAssetBrowserState::SetTreeWidth(int width) noexcept {
    view_.SetTreeWidth(width);
}

void EditorAssetBrowserState::BeginTreeWidthDrag() noexcept {
    view_.BeginTreeWidthDrag();
    contextMenu_.Close();
}

void EditorAssetBrowserState::EndTreeWidthDrag() noexcept {
    view_.EndTreeWidthDrag();
}

void EditorAssetBrowserState::SetTreeScrollOffset(int offset, int maxOffset) noexcept {
    view_.SetTreeScrollOffset(offset, maxOffset);
}

void EditorAssetBrowserState::BeginTreeScrollbarDrag(int y) noexcept {
    view_.BeginTreeScrollbarDrag(y);
    contextMenu_.Close();
}

void EditorAssetBrowserState::DragTreeScrollbar(int y, int trackTravel, int maxOffset) noexcept {
    view_.DragTreeScrollbar(y, trackTravel, maxOffset);
}

void EditorAssetBrowserState::EndTreeScrollbarDrag() noexcept {
    view_.EndTreeScrollbarDrag();
}

void EditorAssetBrowserState::SetContentScrollOffset(int offset, int maxOffset) noexcept {
    view_.SetContentScrollOffset(offset, maxOffset);
}

void EditorAssetBrowserState::BeginContentScrollbarDrag(int y) noexcept {
    view_.BeginContentScrollbarDrag(y);
    contextMenu_.Close();
}

void EditorAssetBrowserState::DragContentScrollbar(int y, int trackTravel, int maxOffset) noexcept {
    view_.DragContentScrollbar(y, trackTravel, maxOffset);
}

void EditorAssetBrowserState::EndContentScrollbarDrag() noexcept {
    view_.EndContentScrollbarDrag();
}

bool EditorAssetBrowserState::SetDeleteConfirmListScrollOffset(int offset, int maxOffset) noexcept {
    const int clamped = std::clamp(offset, 0, std::max(0, maxOffset));
    if (deleteConfirmListScrollOffset_ == clamped) {
        return false;
    }
    deleteConfirmListScrollOffset_ = clamped;
    return true;
}

void EditorAssetBrowserState::BeginDeleteConfirmListScrollbarDrag(int y) noexcept {
    deleteConfirmListScrollbarDragging_ = true;
    deleteConfirmListScrollbarDragY_ = y;
    deleteConfirmListScrollbarDragStartOffset_ = deleteConfirmListScrollOffset_;
}

void EditorAssetBrowserState::DragDeleteConfirmListScrollbar(int y, int trackTravel, int maxOffset) noexcept {
    if (!deleteConfirmListScrollbarDragging_) {
        return;
    }
    const int delta = y - deleteConfirmListScrollbarDragY_;
    static_cast<void>(SetDeleteConfirmListScrollOffset(deleteConfirmListScrollbarDragStartOffset_ + (delta * maxOffset) / std::max(1, trackTravel), maxOffset));
}

void EditorAssetBrowserState::EndDeleteConfirmListScrollbarDrag() noexcept {
    deleteConfirmListScrollbarDragging_ = false;
}

bool EditorAssetBrowserState::IsDeleteTargetChecked(std::string_view key) const {
    return uncheckedDeleteTargets_.find(std::string(key)) == uncheckedDeleteTargets_.end();
}

bool EditorAssetBrowserState::ToggleDeleteTargetChecked(std::string_view key) {
    const std::string normalizedKey(key);
    if (normalizedKey.empty()) {
        return false;
    }
    const auto [it, inserted] = uncheckedDeleteTargets_.insert(normalizedKey);
    if (!inserted) {
        uncheckedDeleteTargets_.erase(it);
    }
    return true;
}

} // namespace kb::editor
