#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "commands/EditorCommandStack.hpp"
#include "console/EditorConsoleState.hpp"
#include "scene/EditorHierarchyExpansionState.hpp"
#include "scene/EditorHierarchyRow.hpp"
#include "scene/EditorHierarchySearchState.hpp"
#include "scene/EditorHierarchySelectionState.hpp"
#include "scene/EditorSceneViewportStateStore.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <string>
#include <string_view>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace kb::editor {

class EditorSceneCommandController;

class EditorSceneContext {
public:
    EditorSceneContext();

    [[nodiscard]] kb::scene::Scene& Scene() noexcept;
    [[nodiscard]] const kb::scene::Scene& Scene() const noexcept;
    [[nodiscard]] EditorAssetBrowserState& AssetBrowser() noexcept;
    [[nodiscard]] const EditorAssetBrowserState& AssetBrowser() const noexcept;
    [[nodiscard]] EditorViewportPreviewState& ViewportPreview() noexcept;
    [[nodiscard]] const EditorViewportPreviewState& ViewportPreview() const noexcept;
    [[nodiscard]] EditorViewportPreviewState& ViewportPreview(std::uint64_t viewportKey) noexcept;
    [[nodiscard]] const EditorViewportPreviewState& ViewportPreview(std::uint64_t viewportKey) const noexcept;
    [[nodiscard]] EditorViewportCameraState& ViewportCamera() noexcept;
    [[nodiscard]] const EditorViewportCameraState& ViewportCamera() const noexcept;
    [[nodiscard]] EditorViewportCameraState& ViewportCamera(std::uint64_t viewportKey) noexcept;
    [[nodiscard]] const EditorViewportCameraState& ViewportCamera(std::uint64_t viewportKey) const noexcept;
    void BeginViewportCameraNavigation(std::uint64_t viewportKey, EditorViewportCameraNavigationMode mode, int x, int y) noexcept;
    [[nodiscard]] bool HasActiveViewportCameraNavigation() const noexcept;
    [[nodiscard]] std::uint64_t ActiveViewportCameraKey() const noexcept;
    [[nodiscard]] EditorViewportCameraState* ActiveViewportCamera() noexcept;
    [[nodiscard]] const EditorViewportCameraState* ActiveViewportCamera() const noexcept;
    void EndViewportCameraNavigation() noexcept;
    [[nodiscard]] bool CloseViewportToolbarDropdowns() noexcept;
    [[nodiscard]] InspectorPanelState& Inspector() noexcept;
    [[nodiscard]] const InspectorPanelState& Inspector() const noexcept;
    [[nodiscard]] EditorConsoleState& Console() noexcept;
    [[nodiscard]] const EditorConsoleState& Console() const noexcept;
    [[nodiscard]] EditorSceneGizmoState& Gizmo() noexcept;
    [[nodiscard]] const EditorSceneGizmoState& Gizmo() const noexcept;
    [[nodiscard]] const kb::project::ProjectDescriptor& Project() const noexcept;
    [[nodiscard]] const std::filesystem::path& ProjectFile() const noexcept;
    [[nodiscard]] const std::filesystem::path& CurrentScenePath() const noexcept;
    [[nodiscard]] std::uint64_t SceneRenderRevision() const noexcept;
    void MarkSceneRenderDirty() noexcept;
    [[nodiscard]] bool NewScene();
    [[nodiscard]] bool OpenDefaultScene();
    [[nodiscard]] bool SaveCurrentScene();
    [[nodiscard]] bool CanUndoSceneCommand() const noexcept;
    [[nodiscard]] bool CanRedoSceneCommand() const noexcept;
    [[nodiscard]] bool UndoSceneCommand();
    [[nodiscard]] bool RedoSceneCommand();
    [[nodiscard]] bool BeginSceneEditTransaction(std::string label);
    [[nodiscard]] bool CommitSceneEditTransaction();
    void CancelSceneEditTransaction();
    [[nodiscard]] bool HasPendingSceneEditTransaction() const noexcept;

    [[nodiscard]] kb::scene::SceneEntity SelectedEntity() const noexcept;
    [[nodiscard]] const std::vector<kb::scene::SceneEntity>& SelectedHierarchyEntities() const noexcept;
    [[nodiscard]] bool IsHierarchyEntitySelected(kb::scene::SceneEntity entity) const noexcept;
    void SelectEntity(kb::scene::SceneEntity entity) noexcept;
    void ClearHierarchySelection() noexcept;
    [[nodiscard]] bool SelectHierarchyRow(std::size_t rowIndex) noexcept;
    [[nodiscard]] bool SelectHierarchyRow(std::size_t rowIndex, bool additive, bool range) noexcept;

    [[nodiscard]] std::vector<EditorHierarchyRow> HierarchyRows() const;
    [[nodiscard]] int HierarchyScrollOffset() const noexcept;
    [[nodiscard]] bool IsHierarchyScrollbarDragging() const noexcept;
    [[nodiscard]] bool SetHierarchyScrollOffset(int offset, int maxOffset) noexcept;
    void BeginHierarchyScrollbarDrag(int y) noexcept;
    void DragHierarchyScrollbar(int y, int trackTravel, int maxOffset) noexcept;
    void EndHierarchyScrollbarDrag() noexcept;
    [[nodiscard]] std::string_view HierarchySearchQuery() const noexcept;
    [[nodiscard]] bool IsHierarchySearchFocused() const noexcept;
    [[nodiscard]] bool IsHierarchyRenaming() const noexcept;
    [[nodiscard]] bool IsHierarchyRenaming(kb::scene::SceneEntity entity) const noexcept;
    [[nodiscard]] bool IsHierarchyRenameSelectingAll() const noexcept;
    [[nodiscard]] std::string_view HierarchyRenameBuffer() const noexcept;

    void FocusHierarchySearch(bool focused) noexcept;
    void SetHierarchySearchQuery(std::string query);
    void AppendHierarchySearchText(wchar_t character);
    void InsertHierarchySearchText(std::string_view text);
    void BackspaceHierarchySearch();
    void SelectAllHierarchySearch() noexcept;
    void ClearHierarchySearch();
    [[nodiscard]] bool BeginHierarchyRename();
    void AppendHierarchyRenameText(wchar_t character);
    void InsertHierarchyRenameText(std::string_view text);
    void SetHierarchyRenameText(std::string text);
    void BackspaceHierarchyRename();
    void SelectAllHierarchyRename() noexcept;
    void ClearHierarchyRename() noexcept;
    [[nodiscard]] bool CommitHierarchyRename();
    void CancelHierarchyRename() noexcept;
    [[nodiscard]] bool BeginAssetFolderCreation();
    [[nodiscard]] bool BeginAssetRename();
    [[nodiscard]] bool BeginAssetRename(kb::assets::AssetId id);
    [[nodiscard]] bool BeginAssetFolderRename(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CommitAssetTextEdit();
    void CancelAssetTextEdit() noexcept;
    [[nodiscard]] bool DeleteSelectedAssetBrowserItem();
    [[nodiscard]] bool DeleteSelectedHierarchyEntity() noexcept;
    [[nodiscard]] bool DuplicateSelectedHierarchyEntities();
    [[nodiscard]] bool DeleteAssetBrowserItem(kb::assets::AssetId id);
    [[nodiscard]] bool DeleteAssetBrowserFolder(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool MoveAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] bool MoveAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] bool CopyAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] bool CopyAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] bool ImportAssetFiles(std::span<const std::filesystem::path> sourceFiles);
    [[nodiscard]] bool ImportAssetFiles(std::span<const std::filesystem::path> sourceFiles, const std::filesystem::path& destinationVirtualFolder);

    [[nodiscard]] bool ToggleHierarchyRowExpanded(std::size_t rowIndex);
    [[nodiscard]] bool ToggleEntityVisibility(kb::scene::SceneEntity entity);
    [[nodiscard]] kb::scene::SceneEntity CreateHierarchyObject();
    [[nodiscard]] bool ReparentEntity(kb::scene::SceneEntity child, kb::scene::SceneEntity parent);
    [[nodiscard]] bool ReparentEntities(std::span<const kb::scene::SceneEntity> children, kb::scene::SceneEntity parent);
    [[nodiscard]] bool CreatePrefabAsset(kb::scene::SceneEntity entity, const std::filesystem::path& path);
    [[nodiscard]] bool InstantiatePrefabAsset(const std::filesystem::path& path, kb::scene::SceneEntity parent);
    [[nodiscard]] bool InstantiatePrefabAsset(const std::filesystem::path& path, const std::filesystem::path& virtualPath, kb::scene::SceneEntity parent);
    [[nodiscard]] kb::scene::SceneEntity CreateMeshAssetEntity(kb::assets::AssetId assetId);
    [[nodiscard]] kb::scene::SceneEntity CreateMeshAssetEntity(kb::assets::AssetId assetId, kb::scene::Vec3 position, bool logCreation);
    [[nodiscard]] bool AddBehaviourAssetToEntity(kb::assets::AssetId assetId, kb::scene::SceneEntity entity);

private:
    [[nodiscard]] EditorSceneCommandController SceneCommands() noexcept;
    [[nodiscard]] bool ExecuteSceneCommand(std::string label, std::function<bool()> mutation);
    void ResetSceneEditState();
    void SelectFirstSceneEntityOrClear() noexcept;
    [[nodiscard]] std::filesystem::path ResolveProjectVirtualPath(const std::filesystem::path& virtualPath) const;
    [[nodiscard]] std::filesystem::path ResolveDefaultScenePath() const;

    kb::scene::Scene scene_;
    kb::project::ProjectDescriptor project_;
    std::filesystem::path projectFile_;
    std::filesystem::path currentScenePath_;
    EditorAssetBrowserState assetBrowser_;
    EditorConsoleState console_;
    EditorSceneViewportStateStore viewportState_;
    InspectorPanelState inspector_;
    EditorCommandStack commandStack_;
    EditorHierarchySelectionState hierarchySelection_;
    EditorHierarchyExpansionState hierarchyExpansion_;
    EditorHierarchySearchState hierarchySearch_;
    kb::scene::SceneEntity hierarchyRenameEntity_{};
    std::string hierarchyRenameBuffer_;
    bool hierarchyRenameSelectingAll_ = false;
    std::optional<std::string> pendingSceneTransactionLabel_;
    std::uint64_t sceneRenderRevision_ = 1U;
    int hierarchyScrollOffset_ = 0;
    int hierarchyScrollbarDragY_ = 0;
    int hierarchyScrollbarDragStartOffset_ = 0;
    bool hierarchyScrollbarDragging_ = false;
};

} // namespace kb::editor
