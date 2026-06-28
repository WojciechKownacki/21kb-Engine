#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "commands/EditorCommandStack.hpp"
#include "console/EditorConsoleState.hpp"
#include "scene/EditorHierarchyExpansionState.hpp"
#include "scene/EditorHierarchyRow.hpp"
#include "scene/EditorHierarchySearchState.hpp"
#include "scene/EditorHierarchySelectionState.hpp"
#include "scene/EditorPluginsState.hpp"
#include "scene/EditorProjectSettingsState.hpp"
#include "scene/EditorScriptEditorState.hpp"
#include "scene/EditorSceneObjectEditTypes.hpp"
#include "scene/EditorSceneViewportStateStore.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/MaterialEditorState.hpp"
#include "scene/transform_edit/EditorSceneTransformEditSession.hpp"
#include "app/scene_viewport/EditorSceneViewportSelectionTypes.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "app/EditorPlayModeSceneSession.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <string>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace kb::input {

struct InputActionAsset;
struct InputMappingContextAsset;
enum class InputKey : std::uint16_t;
enum class InputActionValueType : std::uint8_t;

} // namespace kb::input

namespace kb::modules {

class EngineModuleHost;

} // namespace kb::modules

namespace kb::script {

class ScriptModule;

} // namespace kb::script

namespace kb::editor {

class EditorSceneCommandController;
class EditorInputActionAuthoring;
class EditorInputMappingContextAuthoring;
class IEditorMaterialAssetPropertyEdit;
class EditorMaterialAssetAuthoring;
class EditorMaterialPreviewScene;
struct EditorMaterialPreviewTelemetry;

enum class EditorDirtySceneResolution {
    Save,
    Discard,
};

class EditorSceneContext {
public:
    EditorSceneContext();
    ~EditorSceneContext();

    EditorSceneContext(const EditorSceneContext&) = delete;
    EditorSceneContext& operator=(const EditorSceneContext&) = delete;

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
    [[nodiscard]] MaterialEditorState& MaterialEditor() noexcept;
    [[nodiscard]] const MaterialEditorState& MaterialEditor() const noexcept;
    [[nodiscard]] EditorConsoleState& Console() noexcept;
    [[nodiscard]] const EditorConsoleState& Console() const noexcept;
    [[nodiscard]] EditorSceneGizmoState& Gizmo() noexcept;
    [[nodiscard]] const EditorSceneGizmoState& Gizmo() const noexcept;
    [[nodiscard]] EditorProjectSettingsState& ProjectSettings() noexcept;
    [[nodiscard]] const EditorProjectSettingsState& ProjectSettings() const noexcept;
    [[nodiscard]] EditorPluginsState& Plugins() noexcept;
    [[nodiscard]] const EditorPluginsState& Plugins() const noexcept;
    [[nodiscard]] EditorScriptEditorState& ScriptEditor() noexcept;
    [[nodiscard]] const EditorScriptEditorState& ScriptEditor() const noexcept;
    [[nodiscard]] const kb::project::ProjectDescriptor& Project() const noexcept;
    [[nodiscard]] const std::filesystem::path& ProjectFile() const noexcept;
    [[nodiscard]] const std::filesystem::path& CurrentScenePath() const noexcept;
    [[nodiscard]] std::uint64_t SceneRenderRevision() const noexcept;
    [[nodiscard]] std::uint64_t SceneRenderDirtyBaseRevision() const noexcept;
    [[nodiscard]] bool SceneRenderFullDirty() const noexcept;
    [[nodiscard]] const std::vector<std::uint64_t>& SceneRenderDirtyEntityIds() const noexcept;
    [[nodiscard]] bool SceneDocumentDirty() const noexcept;
    void MarkSceneRenderDirty() noexcept;
    void MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity> entities);
    void AcknowledgeSceneRenderSubmitted() noexcept;
    void MarkSceneDocumentDirty() noexcept;
    [[nodiscard]] bool SaveDirtySceneDocument(std::string_view reason);
    void DiscardDirtySceneDocument(std::string_view reason);
    [[nodiscard]] bool PrepareDirtySceneTransition(std::string_view reason, EditorDirtySceneResolution resolution);
    [[nodiscard]] bool BeginPlayModeSceneSession();
    [[nodiscard]] bool RestorePlayModeSceneSession();
    [[nodiscard]] bool HasPlayModeSceneSession() const noexcept;
    [[nodiscard]] bool ReloadSceneFromProject();
    [[nodiscard]] bool NewScene(EditorDirtySceneResolution dirtyResolution = EditorDirtySceneResolution::Save);
    [[nodiscard]] bool OpenDefaultScene();
    [[nodiscard]] bool OpenScene(const std::filesystem::path& path, EditorDirtySceneResolution dirtyResolution = EditorDirtySceneResolution::Save);
    [[nodiscard]] bool SaveCurrentScene();
    [[nodiscard]] bool SaveCurrentSceneAs(const std::filesystem::path& path);
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
    void SelectHierarchyEntities(std::span<const kb::scene::SceneEntity> entities) noexcept;
    void ClearHierarchySelection() noexcept;
    [[nodiscard]] bool SelectHierarchyRow(std::size_t rowIndex) noexcept;
    [[nodiscard]] bool SelectHierarchyRow(std::size_t rowIndex, bool additive, bool range) noexcept;
    [[nodiscard]] const EditorSceneViewportBoxSelectionState& ViewportBoxSelection() const noexcept;
    void BeginViewportBoxSelection(const EditorSceneViewportBoxSelectionState& selection) noexcept;
    void UpdateViewportBoxSelection(POINT current, bool active) noexcept;
    void ClearViewportBoxSelection() noexcept;

    [[nodiscard]] const std::vector<EditorHierarchyRow>& HierarchyRows() const;
    [[nodiscard]] std::size_t HierarchyRowCount() const;
    [[nodiscard]] const EditorHierarchyRow* HierarchyRowAt(std::size_t rowIndex) const;
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
    [[nodiscard]] bool AdoptCreatedHierarchyEntities(std::string label, std::span<const kb::scene::SceneEntity> entities);
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
    [[nodiscard]] kb::scene::SceneEntity CreateLightObject(kb::scene::LightKind kind);
    [[nodiscard]] bool ReparentEntity(kb::scene::SceneEntity child, kb::scene::SceneEntity parent);
    [[nodiscard]] bool ReparentEntities(std::span<const kb::scene::SceneEntity> children, kb::scene::SceneEntity parent);
    [[nodiscard]] bool CreatePrefabAsset(kb::scene::SceneEntity entity, const std::filesystem::path& path);
    [[nodiscard]] bool CreateInputActionAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateInputAxisAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateInputMappingContextAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateMaterialAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateMaterialGraphAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateMaterialInstanceAsset(kb::assets::AssetId parentMaterial);
    [[nodiscard]] bool CreateMaterialTypeAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateMaterialFromGraphAsset(kb::assets::AssetId graphAssetId);
    [[nodiscard]] bool CreateMaterialFromMaterialTypeAsset(kb::assets::AssetId materialTypeAssetId);
    [[nodiscard]] bool DuplicateMaterialAsset(kb::assets::AssetId materialAssetId);
    [[nodiscard]] bool FindMaterialReferences(kb::assets::AssetId materialAssetId);
    [[nodiscard]] bool ExtractEmbeddedMaterials(kb::assets::AssetId meshAssetId);
    [[nodiscard]] bool CreateLuaScriptAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool OpenLuaScript(kb::assets::AssetId id);
    [[nodiscard]] bool OpenMaterialEditorAsset(kb::assets::AssetId id);
    [[nodiscard]] bool OpenMaterialEditorGraphSourceAsset(kb::assets::AssetId id);
    [[nodiscard]] bool OpenMaterialEditorMaterialTypeAsset(kb::assets::AssetId id);
    void CloseMaterialEditorAsset() noexcept;
    [[nodiscard]] bool HasDirtyMaterialAssetEdit() const noexcept;
    [[nodiscard]] bool PrepareMaterialAssetSelectionChange(kb::assets::AssetId nextAsset);
    [[nodiscard]] bool PrepareMaterialEditorClose(std::string_view reason);
    [[nodiscard]] std::optional<kb::input::InputActionAsset> ReadInputActionAsset(kb::assets::AssetId id) const;
    [[nodiscard]] bool SetInputActionName(kb::assets::AssetId id, std::string name);
    [[nodiscard]] bool CycleInputActionValueType(kb::assets::AssetId id);
    [[nodiscard]] bool SetInputActionValueType(kb::assets::AssetId id, kb::input::InputActionValueType valueType);
    [[nodiscard]] bool ToggleInputActionConsume(kb::assets::AssetId id);
    [[nodiscard]] std::optional<kb::render::RenderMaterialAssetData> ReadMaterialAsset(kb::assets::AssetId id) const;
    [[nodiscard]] std::optional<kb::render::RenderMaterialAssetData> ReadMaterialDocumentAsset(kb::assets::AssetId id) const;
    [[nodiscard]] const kb::scene::Scene& MaterialPreviewScene(kb::assets::AssetId id);
    [[nodiscard]] const EditorMaterialPreviewTelemetry& MaterialPreviewTelemetry() const noexcept;
    [[nodiscard]] std::uint64_t MaterialPreviewRevision() const noexcept;
    [[nodiscard]] std::uint32_t SelectedMaterialGraphNodeId() const noexcept;
    [[nodiscard]] bool SelectMaterialGraphNode(std::uint32_t nodeId) noexcept;
    [[nodiscard]] bool ClearMaterialGraphNodeSelection() noexcept;
    void FocusMaterialGraph(bool focused) noexcept;
    [[nodiscard]] bool IsMaterialGraphFocused() const noexcept;
    [[nodiscard]] float MaterialGraphZoom() const noexcept;
    [[nodiscard]] int MaterialGraphPanX() const noexcept;
    [[nodiscard]] int MaterialGraphPanY() const noexcept;
    [[nodiscard]] bool ZoomMaterialGraph(int wheelDelta) noexcept;
    [[nodiscard]] bool ZoomMaterialGraph(int wheelDelta, int focusCanvasX, int focusCanvasY) noexcept;
    [[nodiscard]] bool BeginMaterialGraphNodeDrag(kb::assets::AssetId assetId, std::uint32_t nodeId, int x, int y);
    [[nodiscard]] bool DragMaterialGraphNode(int x, int y);
    [[nodiscard]] bool EndMaterialGraphNodeDrag();
    [[nodiscard]] bool IsMaterialGraphNodeDragging() const noexcept;
    [[nodiscard]] bool BeginMaterialGraphPan(int x, int y) noexcept;
    [[nodiscard]] bool DragMaterialGraphPan(int x, int y) noexcept;
    [[nodiscard]] bool EndMaterialGraphPan() noexcept;
    [[nodiscard]] bool IsMaterialGraphPanning() const noexcept;
    [[nodiscard]] bool HasMaterialGraphPanMoved() const noexcept;
    [[nodiscard]] int MaterialGraphNodeOffsetX(kb::assets::AssetId assetId, std::uint32_t nodeId) const noexcept;
    [[nodiscard]] int MaterialGraphNodeOffsetY(kb::assets::AssetId assetId, std::uint32_t nodeId) const noexcept;
    [[nodiscard]] bool AddMaterialGraphNode(
        kb::assets::AssetId id,
        kb::render::RenderMaterialGraphNodeKind kind,
        int graphX,
        int graphY);
    [[nodiscard]] bool DeleteSelectedMaterialGraphNode(kb::assets::AssetId id);
    [[nodiscard]] bool DisconnectSelectedMaterialGraphNodeLinks(kb::assets::AssetId id);
    [[nodiscard]] bool SetMaterialGraphTextureSampleAsset(kb::assets::AssetId id, std::uint32_t nodeId, kb::assets::AssetId textureId);
    [[nodiscard]] bool BeginMaterialGraphPinConnection(kb::assets::AssetId id, std::uint32_t nodeId, std::string pin);
    [[nodiscard]] bool BeginMaterialGraphPinConnection(
        kb::assets::AssetId id,
        std::uint32_t nodeId,
        std::string pin,
        bool outputPin,
        int x,
        int y);
    [[nodiscard]] bool DragMaterialGraphPinConnection(int x, int y) noexcept;
    [[nodiscard]] bool CompleteMaterialGraphPinConnection(kb::assets::AssetId id, std::uint32_t toNodeId, std::string toPin);
    [[nodiscard]] bool CompleteMaterialGraphPinConnection(
        kb::assets::AssetId id,
        std::uint32_t nodeId,
        std::string pin,
        bool inputPin);
    [[nodiscard]] bool DisconnectMaterialGraphInputPin(kb::assets::AssetId id, std::uint32_t toNodeId, std::string_view toPin);
    [[nodiscard]] bool DisconnectMaterialGraphOutputPin(kb::assets::AssetId id, std::uint32_t fromNodeId, std::string_view fromPin);
    [[nodiscard]] bool DisconnectMaterialGraphLink(
        kb::assets::AssetId id,
        std::uint32_t fromNodeId,
        std::string_view fromPin,
        std::uint32_t toNodeId,
        std::string_view toPin);
    [[nodiscard]] bool DetachMaterialGraphOutputPinConnection(
        kb::assets::AssetId id,
        std::uint32_t fromNodeId,
        std::string_view fromPin,
        int x,
        int y);
    void CancelMaterialGraphPinConnection() noexcept;
    [[nodiscard]] bool HasMaterialGraphPinConnection() const noexcept;
    [[nodiscard]] kb::assets::AssetId MaterialGraphPinConnectionAssetId() const noexcept;
    [[nodiscard]] std::uint32_t MaterialGraphPinConnectionNodeId() const noexcept;
    [[nodiscard]] std::string_view MaterialGraphPinConnectionPin() const noexcept;
    [[nodiscard]] bool MaterialGraphPinConnectionIsOutput() const noexcept;
    [[nodiscard]] int MaterialGraphPinConnectionX() const noexcept;
    [[nodiscard]] int MaterialGraphPinConnectionY() const noexcept;
    [[nodiscard]] bool OpenMaterialGraphContextMenu(kb::assets::AssetId id, int x, int y, int graphX, int graphY) noexcept;
    [[nodiscard]] bool CloseMaterialGraphContextMenu() noexcept;
    [[nodiscard]] bool IsMaterialGraphContextMenuOpen() const noexcept;
    [[nodiscard]] int MaterialGraphContextMenuX() const noexcept;
    [[nodiscard]] int MaterialGraphContextMenuY() const noexcept;
    [[nodiscard]] int MaterialGraphContextMenuGraphX() const noexcept;
    [[nodiscard]] int MaterialGraphContextMenuGraphY() const noexcept;
    [[nodiscard]] bool IsMaterialGraphContextMenuCategoryExpanded(std::size_t categoryIndex) const noexcept;
    [[nodiscard]] bool IsMaterialGraphContextMenuCategoryHovered(std::size_t categoryIndex) const noexcept;
    [[nodiscard]] bool IsMaterialGraphContextMenuCommandHovered(std::size_t categoryIndex, MaterialEditorGraphMenuCommand command) const noexcept;
    [[nodiscard]] bool SetMaterialGraphContextMenuHover(std::size_t categoryIndex, MaterialEditorGraphMenuCommand command) noexcept;
    [[nodiscard]] bool ClearMaterialGraphContextMenuHover() noexcept;
    [[nodiscard]] bool ToggleMaterialGraphContextMenuCategory(std::size_t categoryIndex) noexcept;
    [[nodiscard]] bool ExecuteMaterialGraphContextMenuCommand(MaterialEditorGraphMenuCommand command);
    [[nodiscard]] bool SetMaterialEditorGraphParameterValue(
        kb::assets::AssetId id,
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type,
        std::string_view valueText);
    [[nodiscard]] bool SetMaterialGraphConstantValue(
        kb::assets::AssetId id,
        std::uint32_t nodeId,
        std::string_view valueText);
    [[nodiscard]] bool SetMaterialBaseColor(kb::assets::AssetId id, int channel, float value);
    [[nodiscard]] bool SetMaterialEmissiveColor(kb::assets::AssetId id, int channel, float value);
    [[nodiscard]] bool SetMaterialMetallicFactor(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetMaterialRoughnessFactor(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetMaterialNormalScale(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetMaterialOcclusionStrength(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetMaterialEmissiveStrength(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetMaterialAlphaCutoff(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetMaterialAlphaMode(kb::assets::AssetId id, kb::render::RenderMaterialAlphaMode mode);
    [[nodiscard]] bool CycleMaterialAlphaMode(kb::assets::AssetId id);
    [[nodiscard]] bool ToggleMaterialDoubleSided(kb::assets::AssetId id);
    [[nodiscard]] bool SetMaterialTextureAsset(kb::assets::AssetId id, EditorMaterialTextureSlot slot, kb::assets::AssetId textureId);
    [[nodiscard]] bool CycleMaterialTextureAsset(kb::assets::AssetId id, EditorMaterialTextureSlot slot);
    [[nodiscard]] bool SaveMaterialEditorAsset(kb::assets::AssetId id);
    [[nodiscard]] bool RevertMaterialEditorAsset(kb::assets::AssetId id);
    [[nodiscard]] bool ValidateMaterialEditorAsset(kb::assets::AssetId id);
    [[nodiscard]] bool BeginMaterialAssetFloatEdit(kb::assets::AssetId id, InspectorPropertyId property);
    [[nodiscard]] bool ApplyActiveMaterialAssetFloatEdit(float value);
    [[nodiscard]] bool CommitActiveMaterialAssetEdit();
    void CancelActiveMaterialAssetEdit() noexcept;
    [[nodiscard]] bool HasActiveMaterialAssetEdit() const noexcept;

    [[nodiscard]] std::vector<std::string> ProjectInputMappingContextOptions() const;
    [[nodiscard]] bool SetProjectInputMappingContext(std::string virtualPath);
    [[nodiscard]] bool ToggleProjectInputEnabled();
    bool CloseProjectSettingsDropdowns() noexcept;
    [[nodiscard]] bool IsProjectPluginEnabled(std::string_view pluginId) const noexcept;
    [[nodiscard]] std::string ProjectPluginBinaryPath(std::string_view pluginId) const;
    [[nodiscard]] bool ToggleProjectPlugin(std::size_t catalogIndex);

    [[nodiscard]] std::optional<kb::input::InputMappingContextAsset> ReadInputMappingContextAsset(kb::assets::AssetId id) const;
    [[nodiscard]] bool AddInputMapping(kb::assets::AssetId id);
    [[nodiscard]] bool RemoveInputMapping(kb::assets::AssetId id, std::size_t index);
    [[nodiscard]] bool SetInputMappingKey(kb::assets::AssetId id, std::size_t index, kb::input::InputKey key);
    [[nodiscard]] bool SetInputMappingScale(kb::assets::AssetId id, std::size_t index, float scale);
    [[nodiscard]] bool CycleInputMappingAction(kb::assets::AssetId id, std::size_t index);
    [[nodiscard]] bool CycleInputMappingTrigger(kb::assets::AssetId id, std::size_t index);
    [[nodiscard]] bool InstantiatePrefabAsset(const std::filesystem::path& path, kb::scene::SceneEntity parent);
    [[nodiscard]] bool InstantiatePrefabAsset(const std::filesystem::path& path, const std::filesystem::path& virtualPath, kb::scene::SceneEntity parent);
    [[nodiscard]] bool InstantiatePrefabAssetAt(
        const std::filesystem::path& path,
        const std::filesystem::path& virtualPath,
        kb::scene::Vec3 position);
    [[nodiscard]] kb::scene::SceneEntity CreateMeshAssetEntity(kb::assets::AssetId assetId);
    [[nodiscard]] kb::scene::SceneEntity CreateMeshAssetEntity(kb::assets::AssetId assetId, kb::scene::Vec3 position, bool logCreation);
    [[nodiscard]] bool SetMeshRendererMeshAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool AddBehaviourAssetToEntity(kb::assets::AssetId assetId, kb::scene::SceneEntity entity);
    [[nodiscard]] bool SetMeshRendererMaterialAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool ApplyMaterialToSelectedMeshRenderers(kb::assets::AssetId assetId);
    [[nodiscard]] bool CycleMeshRendererMaterialAsset(kb::scene::SceneEntity entity);
    [[nodiscard]] bool SetMeshRendererMaterialSlotAsset(kb::scene::SceneEntity entity, std::uint32_t slotIndex, kb::assets::AssetId assetId);
    [[nodiscard]] bool CycleMeshRendererMaterialSlotAsset(kb::scene::SceneEntity entity, std::uint32_t slotIndex);

    // Inspector-driven script behaviour authoring.
    [[nodiscard]] bool HasEntityScript(kb::scene::SceneEntity entity) const;
    [[nodiscard]] std::string EntityScriptName(kb::scene::SceneEntity entity) const;
    [[nodiscard]] bool EntityScriptEnabled(kb::scene::SceneEntity entity) const;
    [[nodiscard]] std::vector<std::pair<kb::assets::AssetId, std::string>> AvailableScriptAssets() const;
    [[nodiscard]] bool AttachScriptToEntity(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool RemoveScriptFromEntity(kb::scene::SceneEntity entity);
    [[nodiscard]] bool ToggleEntityScriptEnabled(kb::scene::SceneEntity entity);
    [[nodiscard]] bool AddComponentToEntity(kb::scene::SceneEntity entity, std::string_view componentId);
    [[nodiscard]] bool SetAudioSourceClipAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool BeginSelectedTransformEdit(std::string label);
    [[nodiscard]] bool ApplyActiveTransformEditPrimaryPosition(kb::scene::Vec3 position);
    [[nodiscard]] bool ApplyActiveTransformEditPrimaryRotation(kb::scene::Vec3 rotation);
    [[nodiscard]] bool ApplyActiveTransformEditRotationDelta(kb::scene::Quat delta);
    [[nodiscard]] bool ApplyActiveTransformEditPrimaryScale(kb::scene::Vec3 scale);
    [[nodiscard]] bool ApplyActiveTransformEditProperty(InspectorPropertyId property, float value);
    [[nodiscard]] float ActiveTransformEditPropertyStart(InspectorPropertyId property) const noexcept;
    [[nodiscard]] bool CommitActiveTransformEdit();
    void CancelActiveTransformEdit() noexcept;
    [[nodiscard]] bool HasActiveTransformEdit() const noexcept;

private:
    [[nodiscard]] EditorSceneCommandController SceneCommands() noexcept;
    [[nodiscard]] bool ExecuteSceneCommand(std::string label, std::function<bool()> mutation);
    [[nodiscard]] bool ExecuteMaterialAssetEdit(kb::assets::AssetId id, std::unique_ptr<IEditorMaterialAssetPropertyEdit> edit);
    [[nodiscard]] bool RecordMaterialGraphWorkingCopyEdit(
        kb::assets::AssetId id,
        std::string label,
        kb::render::RenderMaterialAssetData before,
        std::uint32_t beforeSelectedNodeId);
    [[nodiscard]] std::optional<kb::render::RenderMaterialAssetData> MaterialSourceForEdit(kb::assets::AssetId id) const;
    [[nodiscard]] bool ApplyPatchToMaterialEditorWorkingCopy(kb::assets::AssetId id, IEditorMaterialAssetPropertyEdit& edit);
    [[nodiscard]] bool CopyWorkingMaterialToSource(kb::assets::AssetId id);
    void RefreshOpenMaterialEditorFromSource();
    [[nodiscard]] EditorInputActionAuthoring InputActionAuthoring() noexcept;
    [[nodiscard]] EditorInputMappingContextAuthoring InputMappingContextAuthoring() noexcept;
    [[nodiscard]] EditorMaterialAssetAuthoring MaterialAssetAuthoring() noexcept;
    void ActivateProjectInput();
    void EnsureScriptRuntime();
    void ResetScriptRuntimeStateForPlayMode();
    [[nodiscard]] bool SaveProjectDescriptor();
    void ClearSceneDocumentDirty() noexcept;
    void InvalidateHierarchyRows() noexcept;
    void RebuildHierarchyRowsIfNeeded() const;
    void ResetSceneEditState();
    void SelectFirstSceneEntityOrClear() noexcept;
    [[nodiscard]] bool SaveSceneToPath(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path ResolveProjectVirtualPath(const std::filesystem::path& virtualPath) const;
    [[nodiscard]] std::filesystem::path ResolveDefaultScenePath() const;

    // Declared before scene_ so the scene can be constructed from the loaded project
    // descriptor: the project's enabled/disabled module set must be known before the
    // scene wires its subsystems through the engine module host.
    EditorProjectBootstrapResult projectBootstrap_;
    kb::project::ProjectDescriptor project_;
    std::filesystem::path projectFile_;
    std::unique_ptr<kb::scene::Scene> scene_;
    std::filesystem::path currentScenePath_;
    EditorAssetBrowserState assetBrowser_;
    EditorConsoleState console_;
    EditorSceneViewportStateStore viewportState_;
    InspectorPanelState inspector_;
    MaterialEditorState materialEditor_;
    EditorProjectSettingsState projectSettings_;
    EditorPluginsState plugins_;
    EditorScriptEditorState scriptEditor_;
    std::unique_ptr<EditorMaterialPreviewScene> materialPreviewScene_;
    EditorCommandStack commandStack_;
    EditorHierarchySelectionState hierarchySelection_;
    EditorSceneViewportBoxSelectionState viewportBoxSelection_{};
    EditorHierarchyExpansionState hierarchyExpansion_;
    EditorHierarchySearchState hierarchySearch_;
    mutable std::vector<EditorHierarchyRow> hierarchyRowsCache_;
    mutable bool hierarchyRowsDirty_ = true;
    kb::scene::SceneEntity hierarchyRenameEntity_{};
    std::string hierarchyRenameBuffer_;
    bool hierarchyRenameSelectingAll_ = false;
    std::optional<std::string> pendingSceneTransactionLabel_;
    EditorSceneTransformEditSession activeTransformEdit_;
    kb::assets::AssetId activeMaterialEditAsset_{};
    InspectorPropertyId activeMaterialEditProperty_ = InspectorPropertyId::None;
    std::optional<kb::render::RenderMaterialAssetData> activeMaterialEditBefore_;
    struct MaterialGraphNodeViewOffset {
        kb::assets::AssetId assetId{};
        std::uint32_t nodeId = 0U;
        int offsetX = 0;
        int offsetY = 0;
    };
    float materialGraphZoom_ = 0.72F;
    int materialGraphPanX_ = 0;
    int materialGraphPanY_ = 0;
    kb::assets::AssetId materialGraphDragAssetId_{};
    std::uint32_t materialGraphDragNodeId_ = 0U;
    int materialGraphDragStartX_ = 0;
    int materialGraphDragStartY_ = 0;
    int materialGraphDragStartOffsetX_ = 0;
    int materialGraphDragStartOffsetY_ = 0;
    int materialGraphDragStartNodeX_ = 0;
    int materialGraphDragStartNodeY_ = 0;
    std::optional<kb::render::RenderMaterialAssetData> materialGraphDragStartDocument_;
    std::uint32_t materialGraphDragStartSelectedNodeId_ = 0U;
    bool materialGraphDragChanged_ = false;
    bool materialGraphNodeDragging_ = false;
    bool materialGraphFocused_ = false;
    int materialGraphPanStartX_ = 0;
    int materialGraphPanStartY_ = 0;
    int materialGraphPanStartOffsetX_ = 0;
    int materialGraphPanStartOffsetY_ = 0;
    bool materialGraphPanning_ = false;
    bool materialGraphPanMoved_ = false;
    kb::assets::AssetId materialGraphPendingConnectionAssetId_{};
    std::uint32_t materialGraphPendingConnectionNodeId_ = 0U;
    std::string materialGraphPendingConnectionPin_;
    bool materialGraphPendingConnectionOutput_ = true;
    int materialGraphPendingConnectionX_ = 0;
    int materialGraphPendingConnectionY_ = 0;
    kb::assets::AssetId materialGraphContextMenuAssetId_{};
    int materialGraphContextMenuX_ = 0;
    int materialGraphContextMenuY_ = 0;
    int materialGraphContextMenuGraphX_ = 0;
    int materialGraphContextMenuGraphY_ = 0;
    std::uint32_t materialGraphContextMenuExpandedMask_ = 0U;
    std::size_t materialGraphContextMenuHoveredCategory_ = static_cast<std::size_t>(-1);
    MaterialEditorGraphMenuCommand materialGraphContextMenuHoveredCommand_ = MaterialEditorGraphMenuCommand::None;
    std::uint64_t sceneRenderRevision_ = 1U;
    std::uint64_t sceneRenderDirtyBaseRevision_ = 1U;
    std::vector<std::uint64_t> sceneRenderDirtyEntityIds_;
    bool sceneRenderFullDirty_ = true;
    bool sceneDocumentDirty_ = false;
    EditorPlayModeSceneSession playModeSceneSession_;
    int hierarchyScrollOffset_ = 0;
    int hierarchyScrollbarDragY_ = 0;
    int hierarchyScrollbarDragStartOffset_ = 0;
    bool hierarchyScrollbarDragging_ = false;
    // Declared last so it is destroyed before scene_: the script module installs
    // a scene system that references the scene.
    kb::script::ScriptModule* scriptModule_ = nullptr;
    std::unique_ptr<kb::modules::EngineModuleHost> scriptModuleHost_;
};

} // namespace kb::editor
