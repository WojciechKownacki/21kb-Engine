#pragma once

#include "assets/EditorAssetBrowserContextMenuState.hpp"
#include "assets/EditorAssetBrowserDeleteConfirmState.hpp"
#include "assets/EditorAssetBrowserSelectionState.hpp"
#include "assets/EditorAssetBrowserTextEditState.hpp"
#include "assets/EditorAssetBrowserViewState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace kb::editor {

class EditorAssetBrowserState {
public:
    [[nodiscard]] const std::filesystem::path& SelectedFolder() const noexcept;
    [[nodiscard]] const std::filesystem::path& SelectedContentFolder() const noexcept;
    [[nodiscard]] kb::assets::AssetId SelectedAsset() const noexcept;
    [[nodiscard]] kb::assets::AssetId InspectorAsset() const noexcept;
    [[nodiscard]] EditorAssetBrowserSelectionKind SelectionKind() const noexcept;
    [[nodiscard]] std::string_view SearchQuery() const noexcept;
    [[nodiscard]] bool IsSearchFocused() const noexcept;
    [[nodiscard]] bool IsSelectionFocused() const noexcept;
    [[nodiscard]] bool Recursive() const noexcept;
    [[nodiscard]] EditorAssetViewMode ViewMode() const noexcept;
    [[nodiscard]] EditorAssetSortMode SortMode() const noexcept;
    [[nodiscard]] std::string_view TypeFilter() const noexcept;
    [[nodiscard]] bool ShowFolders() const noexcept;
    [[nodiscard]] bool ShowTemplates() const noexcept;
    [[nodiscard]] bool IsFilterMenuOpen() const noexcept;
    [[nodiscard]] int FilterMenuHoveredIndex() const noexcept;
    [[nodiscard]] bool IsSortMenuOpen() const noexcept;
    [[nodiscard]] float ThumbnailScale() const noexcept;
    [[nodiscard]] bool IsThumbnailScaleDragging() const noexcept;
    [[nodiscard]] int TreeWidth() const noexcept;
    [[nodiscard]] bool IsTreeWidthDragging() const noexcept;
    [[nodiscard]] int TreeScrollOffset() const noexcept;
    [[nodiscard]] bool IsTreeScrollbarDragging() const noexcept;
    [[nodiscard]] int ContentScrollOffset() const noexcept;
    [[nodiscard]] bool IsContentScrollbarDragging() const noexcept;
    [[nodiscard]] int DeleteConfirmListScrollOffset() const noexcept;
    [[nodiscard]] bool IsDeleteConfirmListScrollbarDragging() const noexcept;
    [[nodiscard]] EditorAssetTextEditMode TextEditMode() const noexcept;
    [[nodiscard]] kb::assets::AssetId TextEditTargetAsset() const noexcept;
    [[nodiscard]] const std::filesystem::path& TextEditTargetFolder() const noexcept;
    [[nodiscard]] std::string_view TextEditValue() const noexcept;
    [[nodiscard]] bool IsTextEditing() const noexcept;
    [[nodiscard]] bool IsContextMenuOpen() const noexcept;
    [[nodiscard]] bool IsDeleteConfirmOpen() const noexcept;
    [[nodiscard]] bool IsDeleteConfirmDragging() const noexcept;
    [[nodiscard]] int DeleteConfirmOffsetX() const noexcept;
    [[nodiscard]] int DeleteConfirmOffsetY() const noexcept;
    [[nodiscard]] int ContextMenuX() const noexcept;
    [[nodiscard]] int ContextMenuY() const noexcept;
    [[nodiscard]] EditorAssetContextTargetKind ContextMenuTargetKind() const noexcept;
    [[nodiscard]] kb::assets::AssetId ContextMenuTargetAsset() const noexcept;
    [[nodiscard]] const std::filesystem::path& ContextMenuTargetFolder() const noexcept;
    [[nodiscard]] EditorAssetContextCommand ContextMenuHoveredCommand() const noexcept;
    [[nodiscard]] std::vector<EditorAssetContextMenuItem> ContextMenuItems(const kb::assets::AssetManager& manager) const;
    [[nodiscard]] bool IsDropActionMenuOpen() const noexcept;
    [[nodiscard]] int DropActionMenuX() const noexcept;
    [[nodiscard]] int DropActionMenuY() const noexcept;
    [[nodiscard]] const std::filesystem::path& DropActionTargetFolder() const noexcept;
    [[nodiscard]] kb::assets::AssetId DropActionAsset() const noexcept;
    [[nodiscard]] const std::filesystem::path& DropActionSourceFolder() const noexcept;
    [[nodiscard]] EditorAssetDropAction DropActionHoveredCommand() const noexcept;

    void FocusSearch(bool focused) noexcept;
    void FocusSelection(bool focused) noexcept;
    void SetSearchQuery(std::string query);
    void AppendSearchText(wchar_t character);
    void InsertSearchText(std::string_view text);
    void BackspaceSearch();
    void SelectAllSearch() noexcept;
    void ClearSearch();
    void SetRecursive(bool recursive) noexcept;
    void ToggleRecursive() noexcept;
    void SetViewMode(EditorAssetViewMode mode) noexcept;
    void SetSortMode(EditorAssetSortMode mode) noexcept;
    void CycleSortMode() noexcept;
    void CycleTypeFilter(const kb::assets::AssetManager& manager);
    void ToggleFilterMenu() noexcept;
    void CloseFilterMenu() noexcept;
    void ToggleShowFolders() noexcept;
    void ToggleShowTemplates() noexcept;
    [[nodiscard]] bool SetFilterMenuHoveredIndex(int index) noexcept;
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
    [[nodiscard]] bool SetDeleteConfirmListScrollOffset(int offset, int maxOffset) noexcept;
    void BeginDeleteConfirmListScrollbarDrag(int y) noexcept;
    void DragDeleteConfirmListScrollbar(int y, int trackTravel, int maxOffset) noexcept;
    void EndDeleteConfirmListScrollbarDrag() noexcept;
    [[nodiscard]] bool IsDeleteTargetChecked(std::string_view key) const;
    [[nodiscard]] bool ToggleDeleteTargetChecked(std::string_view key);

    void BeginNewFolder();
    [[nodiscard]] bool BeginRenameSelection(const kb::assets::AssetManager& manager);
    [[nodiscard]] bool BeginRenameAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool BeginRenameFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    void SetTextEditValue(std::string value);
    void AppendTextEdit(wchar_t character);
    void InsertTextEdit(std::string_view text);
    void BackspaceTextEdit();
    void ClearTextEdit() noexcept;
    void SelectAllTextEdit() noexcept;
    void CancelTextEdit() noexcept;
    void OpenContextMenuForBackground(int x, int y);
    [[nodiscard]] bool OpenContextMenuForAsset(int x, int y, kb::assets::AssetId id, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool OpenContextMenuForFolder(int x, int y, const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    void CloseContextMenu() noexcept;
    [[nodiscard]] bool OpenDeleteConfirm() noexcept;
    void CloseDeleteConfirm() noexcept;
    void BeginDeleteConfirmDrag(int x, int y) noexcept;
    void DragDeleteConfirm(int x, int y) noexcept;
    void EndDeleteConfirmDrag() noexcept;
    [[nodiscard]] bool SetContextMenuHoveredCommand(EditorAssetContextCommand command) noexcept;
    void OpenDropActionMenuForAsset(int x, int y, kb::assets::AssetId id, const std::filesystem::path& targetFolder);
    void OpenDropActionMenuForFolder(int x, int y, const std::filesystem::path& sourceFolder, const std::filesystem::path& targetFolder);
    void CloseDropActionMenu() noexcept;
    [[nodiscard]] bool SetDropActionHoveredCommand(EditorAssetDropAction command) noexcept;

    [[nodiscard]] bool SelectFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool SelectContentFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool SelectContentFolderAt(std::size_t index, const kb::assets::AssetManager& manager, bool additive, bool range);
    [[nodiscard]] bool SelectAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool SelectAssetAt(std::size_t index, const kb::assets::AssetManager& manager, bool additive, bool range);

    // Plain asset clicks defer selection to pointer-up so a press-and-drag does
    // not change the Inspector. The id is parked here on press and consumed on
    // release (a quick click) to perform the actual selection / preview.
    void SetPendingPreviewAsset(kb::assets::AssetId id) noexcept { pendingPreviewAsset_ = id; }
    void ClearPendingPreviewAsset() noexcept { pendingPreviewAsset_ = {}; }
    [[nodiscard]] bool HasPendingPreviewAsset() const noexcept { return pendingPreviewAsset_.IsValid(); }
    [[nodiscard]] kb::assets::AssetId TakePendingPreviewAsset() noexcept {
        const kb::assets::AssetId id = pendingPreviewAsset_;
        pendingPreviewAsset_ = {};
        return id;
    }
    [[nodiscard]] bool SelectAllContent(const kb::assets::AssetManager& manager);
    [[nodiscard]] bool ToggleFolderExpanded(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    void ClearSelection() noexcept;

    [[nodiscard]] std::vector<EditorAssetFolderRow> FolderRows(const kb::assets::AssetManager& manager) const;
    [[nodiscard]] std::vector<EditorAssetFolderRow> ChildFolderRows(const kb::assets::AssetManager& manager) const;
    [[nodiscard]] std::vector<EditorAssetItemRow> AssetRows(const kb::assets::AssetManager& manager) const;
    [[nodiscard]] std::vector<EditorAssetSelectionSummaryRow> SelectedContentRows(const kb::assets::AssetManager& manager) const;
    [[nodiscard]] std::vector<EditorAssetSelectionSummaryRow> DeleteTargetRows(const kb::assets::AssetManager& manager) const;
    [[nodiscard]] std::vector<EditorAssetSelectionSummaryRow> CheckedDeleteTargetRows(const kb::assets::AssetManager& manager) const;
    [[nodiscard]] std::vector<std::string> AssetTypes(const kb::assets::AssetManager& manager) const;
    [[nodiscard]] const kb::assets::AssetMetadata* SelectedMetadata(const kb::assets::AssetManager& manager) const noexcept;

private:
    [[nodiscard]] bool ContextMenuTargetFolderCanMutate(const kb::assets::AssetManager& manager) const;

    EditorAssetBrowserSelectionState selection_;
    EditorAssetBrowserViewState view_;
    EditorAssetBrowserTextEditState textEdit_;
    EditorAssetBrowserContextMenuState contextMenu_;
    EditorAssetBrowserDeleteConfirmState deleteConfirm_;
    std::size_t contentSelectionAnchor_ = 0;
    bool hasContentSelectionAnchor_ = false;
    int deleteConfirmListScrollOffset_ = 0;
    int deleteConfirmListScrollbarDragY_ = 0;
    int deleteConfirmListScrollbarDragStartOffset_ = 0;
    bool deleteConfirmListScrollbarDragging_ = false;
    std::unordered_set<std::string> uncheckedDeleteTargets_;
    bool dropActionMenuOpen_ = false;
    int filterMenuHoveredIndex_ = -1;
    int dropActionMenuX_ = 0;
    int dropActionMenuY_ = 0;
    kb::assets::AssetId inspectorAsset_{};
    kb::assets::AssetId dropActionAsset_{};
    kb::assets::AssetId pendingPreviewAsset_{};
    std::filesystem::path dropActionSourceFolder_{};
    std::filesystem::path dropActionTargetFolder_{};
    EditorAssetDropAction dropActionHovered_ = EditorAssetDropAction::None;
};

} // namespace kb::editor
