#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "console/EditorConsoleState.hpp"
#include "scene/EditorHierarchyExpansionState.hpp"
#include "scene/EditorHierarchyRow.hpp"
#include "scene/EditorHierarchySearchState.hpp"
#include "scene/EditorHierarchySelectionState.hpp"
#include "scene/EditorViewportCameraState.hpp"
#include "scene/EditorViewportPreviewState.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <string>
#include <string_view>
#include <filesystem>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::editor {

struct EditorSceneGizmoAxisDrag {
    float axis[3]{};
    float planeNormal[3]{};
    float removeNormal[3]{};
    float startPoint[3]{};
};

struct EditorSceneGizmoState {
    int hoveredAxis = -1;
    int draggedAxis = -1;
    bool centerDrag = false;
    float dragStartTargetX = 0.0F;
    float dragStartTargetY = 0.0F;
    float dragStartTargetZ = 0.0F;
    float centerPlaneNx = 0.0F;
    float centerPlaneNy = 0.0F;
    float centerPlaneNz = 1.0F;
    float centerStartPx = 0.0F;
    float centerStartPy = 0.0F;
    float centerStartPz = 0.0F;
    EditorSceneGizmoAxisDrag axisDrag{};

    [[nodiscard]] bool IsDragging() const noexcept {
        return draggedAxis >= 0 || centerDrag;
    }
};

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
    [[nodiscard]] InspectorPanelState& Inspector() noexcept;
    [[nodiscard]] const InspectorPanelState& Inspector() const noexcept;
    [[nodiscard]] EditorConsoleState& Console() noexcept;
    [[nodiscard]] const EditorConsoleState& Console() const noexcept;
    [[nodiscard]] EditorSceneGizmoState& Gizmo() noexcept;
    [[nodiscard]] const EditorSceneGizmoState& Gizmo() const noexcept;

    [[nodiscard]] kb::scene::SceneEntity SelectedEntity() const noexcept;
    [[nodiscard]] const std::vector<kb::scene::SceneEntity>& SelectedHierarchyEntities() const noexcept;
    [[nodiscard]] bool IsHierarchyEntitySelected(kb::scene::SceneEntity entity) const noexcept;
    void SelectEntity(kb::scene::SceneEntity entity) noexcept;
    void ClearHierarchySelection() noexcept;
    [[nodiscard]] bool SelectHierarchyRow(std::size_t rowIndex) noexcept;
    [[nodiscard]] bool SelectHierarchyRow(std::size_t rowIndex, bool additive, bool range) noexcept;

    [[nodiscard]] std::vector<EditorHierarchyRow> HierarchyRows() const;
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
    kb::scene::Scene scene_;
    EditorAssetBrowserState assetBrowser_;
    EditorConsoleState console_;
    EditorSceneGizmoState gizmo_;
    InspectorPanelState inspector_;
    mutable std::unordered_map<std::uint64_t, EditorViewportPreviewState> viewportPreviews_;
    mutable std::unordered_map<std::uint64_t, EditorViewportCameraState> viewportCameras_;
    std::uint64_t activeViewportCameraKey_ = 0U;
    bool hasActiveViewportCameraNavigation_ = false;
    EditorHierarchySelectionState hierarchySelection_;
    EditorHierarchyExpansionState hierarchyExpansion_;
    EditorHierarchySearchState hierarchySearch_;
    kb::scene::SceneEntity hierarchyRenameEntity_{};
    std::string hierarchyRenameBuffer_;
    bool hierarchyRenameSelectingAll_ = false;
};

} // namespace kb::editor
