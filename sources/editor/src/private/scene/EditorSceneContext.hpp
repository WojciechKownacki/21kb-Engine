#pragma once

#include "engine/assets/AssetImportTypes.hpp"
#include "engine/assets/TerrainAsset.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/script/ScriptValue.hpp"
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
#include "scene/AnimationPreviewContext.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/MaterialEditorState.hpp"
#include "scene/material_preview/EditorMaterialPreviewPrimitivePolicy.hpp"
#include "scene/material_preview/EditorMaterialPreviewSettings.hpp"
#include "scene/transform_edit/EditorSceneTransformEditSession.hpp"
#include "app/scene_viewport/EditorSceneViewportSelectionTypes.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "app/EditorPlayModeSceneSession.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "rendering/material_graph/MaterialGraphInteractionPolicy.hpp"

#include <array>
#include <string>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
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

namespace kb::terrain_editor {

struct TerrainBrushSettings;
struct TerrainBrushStamp;
struct TerrainHeightmapImportSettings;
struct TerrainLayerPaintSettings;

} // namespace kb::terrain_editor

namespace kb::editor {

enum class PhysicsComponentKind; // inspection/InspectorPhysicsModel.hpp
class EditorSceneCommandController;
class EditorInputActionAuthoring;
class EditorInputMappingContextAuthoring;
class IEditorMaterialAssetPropertyEdit;
class EditorMaterialAssetAuthoring;
class EditorMaterialPreviewScene;
struct EditorMaterialPreviewTelemetry;
class EditorMaterialGraphCookService;
struct EditorMaterialGraphCookResult;
struct EditorTerrainConfiguration;

enum class EditorMaterialPreviewSurface : std::uint8_t {
    Inspector,
    MaterialEditor,
};

enum class EditorDirtySceneResolution {
    Save,
    Discard,
};

enum class MaterialGraphSelectionOperation : std::uint8_t {
    Replace,
    Add,
    Invert,
    Remove,
};

[[nodiscard]] constexpr MaterialGraphSelectionOperation ResolveMaterialGraphSelectionOperation(
    bool altDown,
    bool controlDown,
    bool shiftDown) noexcept {
    if (altDown) {
        return MaterialGraphSelectionOperation::Remove;
    }
    if (controlDown) {
        return MaterialGraphSelectionOperation::Invert;
    }
    if (shiftDown) {
        return MaterialGraphSelectionOperation::Add;
    }
    return MaterialGraphSelectionOperation::Replace;
}

class EditorSceneContext {
    struct TerrainStrokeState {
        kb::scene::SceneEntity entity{};
        kb::assets::AssetId assetId{};
        kb::assets::TerrainAsset before{};
        kb::assets::TerrainAsset working{};
        std::shared_ptr<kb::render::RenderMeshAssetData> previewMesh{};
        bool changed = false;
        bool previewPublished = false;
        bool layerPaint = false;
        std::string label = "Sculpt Terrain";
    };

    struct TerrainReadCache {
        kb::assets::AssetId assetId{};
        std::uint64_t contentHash = 0U;
        kb::assets::TerrainAsset terrain{};
    };

    struct MaterialGraphDragNodeStart {
        std::uint32_t nodeId = 0U;
        std::int32_t positionX = 0;
        std::int32_t positionY = 0;
    };

public:
    EditorSceneContext();
    ~EditorSceneContext();

    EditorSceneContext(const EditorSceneContext&) = delete;
    EditorSceneContext& operator=(const EditorSceneContext&) = delete;

    [[nodiscard]] kb::scene::Scene& Scene() noexcept;
    [[nodiscard]] const kb::scene::Scene& Scene() const noexcept;
    [[nodiscard]] bool HasCompleteScriptExecutionAffinity() const noexcept;
    [[nodiscard]] EditorAssetBrowserState& AssetBrowser() noexcept;
    [[nodiscard]] const EditorAssetBrowserState& AssetBrowser() const noexcept;
    [[nodiscard]] EditorViewportPreviewState& ViewportPreview() noexcept;
    [[nodiscard]] const EditorViewportPreviewState& ViewportPreview() const noexcept;
    [[nodiscard]] EditorViewportPreviewState& ViewportPreview(std::uint64_t viewportKey) noexcept;
    [[nodiscard]] const EditorViewportPreviewState& ViewportPreview(std::uint64_t viewportKey) const noexcept;
    [[nodiscard]] AnimationPreviewContext& AnimationPreview() noexcept;
    [[nodiscard]] const AnimationPreviewContext& AnimationPreview() const noexcept;
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
    // Frames the current entity selection in the default scene viewport camera
    // (the viewport "F" shortcut): recenters on the selection pivot and pulls
    // back to fit its bounding sphere. Returns false when nothing framable is
    // selected. Starts a short eased animation advanced by
    // TickViewportFocusAnimations.
    [[nodiscard]] bool FrameSelectedEntitiesInViewport() noexcept;
    // Advances any in-progress "frame selected" camera animation; returns true
    // while still animating so the frame loop keeps presenting.
    [[nodiscard]] bool TickViewportFocusAnimations(float deltaSeconds) noexcept;
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
    [[nodiscard]] bool SaveOpenDocuments();
    [[nodiscard]] bool SaveDirtySceneDocument(std::string_view reason);
    void DiscardDirtySceneDocument(std::string_view reason);
    // The rendering host owns GPU resources keyed by Scene::Id(). Registering this
    // callback lets document replacement release those resources while the old scene
    // identity is still alive, without introducing an engine/render dependency here.
    void SetRenderSceneReleaseHandler(
        std::function<void(const kb::scene::Scene&)> handler);
    [[nodiscard]] bool PrepareDirtySceneTransition(std::string_view reason, EditorDirtySceneResolution resolution);
    [[nodiscard]] bool BeginPlayModeSceneSession();
    // Push any per-frame script diagnostics (compile/behaviour errors) the
    // installed scene system produced to the Console, so a behaviour that
    // silently fails to run during play surfaces a reason instead of nothing.
    void SurfaceScriptDiagnostics();
    // Single production play-mode update path shared by the visible editor
    // loop and deterministic automation. Input collection remains owned by
    // the host, but system execution, fault surfacing, diagnostics, render
    // invalidation and quit handling must not diverge between hosts.
    [[nodiscard]] bool TickPlayModeSceneSession(float deltaSeconds);
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
    [[nodiscard]] const kb::assets::TerrainAsset* TerrainForEditing(
        kb::scene::SceneEntity entity,
        std::string* error = nullptr) const;
    [[nodiscard]] bool ApplyTerrainBrushStamp(
        kb::scene::SceneEntity entity,
        const kb::terrain_editor::TerrainBrushSettings& settings,
        const kb::terrain_editor::TerrainBrushStamp& stamp,
        bool beginStroke,
        std::string* error = nullptr);
    [[nodiscard]] bool ApplyTerrainLayerPaintStamp(
        kb::scene::SceneEntity entity,
        const kb::terrain_editor::TerrainLayerPaintSettings& settings,
        const kb::terrain_editor::TerrainBrushStamp& stamp,
        bool beginStroke,
        std::string* error = nullptr);
    [[nodiscard]] bool ApplyTerrainLayerPaintSegment(
        kb::scene::SceneEntity entity,
        const kb::terrain_editor::TerrainLayerPaintSettings& settings,
        const kb::terrain_editor::TerrainBrushStamp& start,
        const kb::terrain_editor::TerrainBrushStamp& end,
        bool beginStroke,
        std::string* error = nullptr);
    [[nodiscard]] bool AddTerrainMaterialLayer(
        kb::scene::SceneEntity entity,
        kb::assets::AssetId materialAssetId,
        std::string* error = nullptr);
    [[nodiscard]] bool SetTerrainMaterialLayer(
        kb::scene::SceneEntity entity,
        std::uint8_t layerIndex,
        kb::assets::AssetId materialAssetId,
        std::string* error = nullptr);
    [[nodiscard]] bool RemoveTerrainMaterialLayer(
        kb::scene::SceneEntity entity,
        std::uint8_t layerIndex,
        std::string* error = nullptr);
    [[nodiscard]] bool CommitTerrainBrushStroke(std::string* error = nullptr);
    void CancelTerrainBrushStroke() noexcept;
    [[nodiscard]] bool ImportTerrainHeightmap(
        kb::scene::SceneEntity entity,
        const std::filesystem::path& path,
        const kb::terrain_editor::TerrainHeightmapImportSettings& settings,
        std::string* error = nullptr);
    [[nodiscard]] bool ConfigureTerrain(
        kb::scene::SceneEntity entity,
        const EditorTerrainConfiguration& configuration,
        std::string* error = nullptr);

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

    // True while the user is typing into ANY inline text field: hierarchy
    // rename or search, Project Files rename / new-folder / search, an
    // inspector field, or a material-graph node rename / constant / find box.
    // Global single-key viewport shortcuts (gizmo W/E/R, camera-frame F) must
    // suppress themselves while this holds, so a typed letter reaches the text
    // field instead of retargeting the tool.
    [[nodiscard]] bool IsAnyInlineTextEditActive() const noexcept;

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
    [[nodiscard]] bool ImportAssetFiles(std::span<const std::filesystem::path> sourceFiles, const std::filesystem::path& destinationVirtualFolder, const kb::assets::AssetImportOptions& options);

    [[nodiscard]] bool ToggleHierarchyRowExpanded(std::size_t rowIndex);
    [[nodiscard]] bool ToggleEntityVisibility(kb::scene::SceneEntity entity);
    [[nodiscard]] bool CycleEntityVisibilityMode(kb::scene::SceneEntity entity);
    [[nodiscard]] bool SetEntityVisibilityMask(kb::scene::SceneEntity entity, std::uint32_t mask);
    [[nodiscard]] bool SetRegionPortalCells(
        kb::scene::SceneEntity entity,
        kb::scene::SceneEntity sourceCell,
        kb::scene::SceneEntity targetCell);
    [[nodiscard]] kb::scene::SceneEntity CreateHierarchyObject();
    [[nodiscard]] kb::scene::SceneEntity CreateLightObject(kb::scene::LightKind kind);
    [[nodiscard]] bool ReparentEntity(kb::scene::SceneEntity child, kb::scene::SceneEntity parent);
    [[nodiscard]] bool ReparentEntities(std::span<const kb::scene::SceneEntity> children, kb::scene::SceneEntity parent);
    [[nodiscard]] bool CreatePrefabAsset(kb::scene::SceneEntity entity, const std::filesystem::path& path);
    [[nodiscard]] bool CreateInputActionAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateInputAxisAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateInputMappingContextAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateMaterialAsset(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool CreateMaterialFunctionAsset(const std::filesystem::path& virtualFolder);
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
    [[nodiscard]] bool OpenAnimationAsset(kb::assets::AssetId id);
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
    [[nodiscard]] std::optional<kb::render::RenderMaterialInstanceAssetData> ReadMaterialInstanceAsset(kb::assets::AssetId id) const;
    [[nodiscard]] std::optional<kb::render::RenderMaterialAssetData> ReadEffectiveMaterialAsset(kb::assets::AssetId id) const;
    [[nodiscard]] std::optional<kb::render::RenderMaterialAssetData> ReadMaterialDocumentAsset(kb::assets::AssetId id) const;
    [[nodiscard]] const kb::scene::Scene& MaterialPreviewScene(
        kb::assets::AssetId id,
        EditorMaterialPreviewSurface surface = EditorMaterialPreviewSurface::MaterialEditor);
    [[nodiscard]] const EditorMaterialPreviewTelemetry& MaterialPreviewTelemetry() const noexcept;
    [[nodiscard]] const EditorMaterialPreviewPrimitivePolicy& MaterialPreviewPrimitivePolicy() const noexcept;
    [[nodiscard]] bool SetMaterialPreviewPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy policy);
    [[nodiscard]] bool CycleMaterialPreviewPrimitive();
    // Thumbnail pipeline: ask the material preview scene to capture its next rendered frame to `path`,
    // then poll. Returns 0 when the scene is unavailable or a capture is already in flight.
    [[nodiscard]] const kb::scene::Scene& MaterialThumbnailScene(kb::assets::AssetId id);
    [[nodiscard]] std::uint64_t RequestMaterialThumbnailCapture(const std::filesystem::path& path);
    [[nodiscard]] kb::scene::SceneScreenCaptureStatus MaterialThumbnailCaptureStatus(std::uint64_t captureId) const noexcept;
    [[nodiscard]] std::uint64_t MaterialThumbnailSceneRevision() const noexcept;
    [[nodiscard]] const EditorMaterialPreviewSceneSettings& MaterialPreviewSceneSettings() const noexcept;
    [[nodiscard]] bool SetMaterialPreviewSceneSettings(EditorMaterialPreviewSceneSettings settings);
    // Orbit (drag) and dolly (wheel) the material preview camera around the framed object, Unreal-style.
    // Camera-only: no re-cook, no scene rebuild.
    [[nodiscard]] bool OrbitMaterialPreviewCamera(float deltaYawDegrees, float deltaPitchDegrees);
    [[nodiscard]] bool ZoomMaterialPreviewCamera(float scale);
    [[nodiscard]] bool CycleMaterialPreviewSceneLightingPreset();
    [[nodiscard]] bool CycleMaterialPreviewQualityLevel();
    [[nodiscard]] bool MaterialPreviewNodePreviewEnabled() const noexcept;
    [[nodiscard]] bool SetMaterialPreviewNodePreviewEnabled(bool enabled) noexcept;
    [[nodiscard]] bool ToggleMaterialPreviewNodePreview() noexcept;
    [[nodiscard]] bool MaterialPreviewNormalDebugViewEnabled() const noexcept;
    [[nodiscard]] bool SetMaterialPreviewNormalDebugViewEnabled(bool enabled);
    [[nodiscard]] bool ToggleMaterialPreviewNormalDebugView();
    // Per-project graph shader cache root shared by the cook service and every renderer (preview
    // panel + scene viewport + play mode) so authored graph programs render identically (MAT-31).
    [[nodiscard]] const std::string& GraphShaderCacheRoot() const noexcept;
    [[nodiscard]] EditorMaterialGraphCookService& MaterialGraphCookService() noexcept;
    // Latest cook result for the material open in the editor (drives the preview status banner).
    [[nodiscard]] EditorMaterialGraphCookResult OpenMaterialGraphCookResult() const;
    // Apply freshly cooked graph programs to live render state (hot reload + status); returns the
    // number of completed cooks consumed this call (MAT-32/33).
    std::size_t PumpMaterialGraphCookResults();
    [[nodiscard]] std::uint64_t MaterialPreviewRevision(
        EditorMaterialPreviewSurface surface = EditorMaterialPreviewSurface::MaterialEditor) const noexcept;
    [[nodiscard]] std::uint32_t SelectedMaterialGraphNodeId() const noexcept;
    [[nodiscard]] const std::vector<std::uint32_t>& SelectedMaterialGraphNodeIds() const noexcept;
    [[nodiscard]] bool IsMaterialGraphNodeSelected(std::uint32_t nodeId) const noexcept;
    [[nodiscard]] bool SelectMaterialGraphNode(std::uint32_t nodeId);
    [[nodiscard]] bool SelectMaterialGraphNode(std::uint32_t nodeId, bool additive, bool toggle);
    [[nodiscard]] bool SetMaterialGraphNodeSelection(std::vector<std::uint32_t> nodeIds, std::uint32_t primaryNodeId);
    [[nodiscard]] bool ClearMaterialGraphNodeSelection();
    [[nodiscard]] std::uint32_t SelectedMaterialGraphCommentId() const noexcept;
    [[nodiscard]] bool IsMaterialGraphCommentSelected(std::uint32_t commentId) const noexcept;
    [[nodiscard]] bool SelectMaterialGraphComment(std::uint32_t commentId);
    [[nodiscard]] bool ClearMaterialGraphCommentSelection();
    [[nodiscard]] bool SelectMaterialGraphContextTarget(std::uint32_t nodeId, std::uint32_t commentId);
    void FocusMaterialGraph(bool focused) noexcept;
    [[nodiscard]] bool IsMaterialGraphFocused() const noexcept;
    [[nodiscard]] float MaterialGraphZoom() const noexcept;
    [[nodiscard]] int MaterialGraphPanX() const noexcept;
    [[nodiscard]] int MaterialGraphPanY() const noexcept;
    [[nodiscard]] bool ZoomMaterialGraph(int wheelDelta) noexcept;
    [[nodiscard]] bool ZoomMaterialGraph(int wheelDelta, int focusCanvasX, int focusCanvasY) noexcept;
    void SetMaterialGraphCanvasViewport(int width, int height) noexcept;
    void SetMaterialGraphCanvasViewport(int left, int top, int width, int height) noexcept;
    [[nodiscard]] int MaterialGraphCanvasLeft() const noexcept;
    [[nodiscard]] int MaterialGraphCanvasTop() const noexcept;
    [[nodiscard]] int MaterialGraphCanvasWidth() const noexcept;
    [[nodiscard]] int MaterialGraphCanvasHeight() const noexcept;
    [[nodiscard]] bool IsMaterialEditorFindFocused() const noexcept;
    void FocusMaterialEditorFind(bool focused) noexcept;
    void SetMaterialEditorFindQuery(std::string query);
    void AppendMaterialEditorFindText(wchar_t character);
    void InsertMaterialEditorFindText(std::string_view text);
    void BackspaceMaterialEditorFind();
    void ClearMaterialEditorFind();
    [[nodiscard]] bool FocusFirstMaterialEditorFindResult();
    [[nodiscard]] bool FocusMaterialEditorFindResult(std::size_t resultIndex, int canvasWidth, int canvasHeight);
    [[nodiscard]] int MaterialEditorDetailsScrollOffset() const noexcept;
    [[nodiscard]] bool SetMaterialEditorDetailsScrollOffset(int offset, int maxOffset) noexcept;
    [[nodiscard]] bool ScrollMaterialEditorDetails(int wheelDelta, int maxOffset) noexcept;
    // Select a node and centre the graph view on it. Used to jump to a diagnostic's offending node.
    [[nodiscard]] bool FocusMaterialGraphNode(std::uint32_t nodeId);
    [[nodiscard]] bool FrameSelectedMaterialGraphNodes();
    [[nodiscard]] bool FrameSelectedMaterialGraphNodes(int canvasWidth, int canvasHeight);
    [[nodiscard]] bool SelectMaterialGraphUpstream();
    [[nodiscard]] bool SelectMaterialGraphDownstream();
    [[nodiscard]] bool AlignSelectedMaterialGraphNodes(kb::assets::AssetId id, MaterialEditorGraphAlignMode mode);
    [[nodiscard]] bool DistributeSelectedMaterialGraphNodes(kb::assets::AssetId id, MaterialEditorGraphDistributeAxis axis);
    [[nodiscard]] bool PromoteSelectedMaterialGraphNodeToParameter(kb::assets::AssetId id);
    [[nodiscard]] bool BeginMaterialGraphNodeDrag(kb::assets::AssetId assetId, std::uint32_t nodeId, int x, int y);
    [[nodiscard]] bool DragMaterialGraphNode(int x, int y);
    [[nodiscard]] bool EndMaterialGraphNodeDrag();
    [[nodiscard]] bool IsMaterialGraphNodeDragging() const noexcept;
    [[nodiscard]] bool BeginMaterialGraphCommentDrag(kb::assets::AssetId assetId, std::uint32_t commentId, int x, int y);
    [[nodiscard]] bool DragMaterialGraphComment(int x, int y);
    [[nodiscard]] bool EndMaterialGraphCommentDrag();
    [[nodiscard]] bool CancelMaterialGraphCommentDrag();
    [[nodiscard]] bool IsMaterialGraphCommentDragging() const noexcept;
    [[nodiscard]] bool BeginMaterialGraphBoxSelection(
        kb::assets::AssetId assetId,
        int x,
        int y,
        MaterialGraphSelectionOperation operation) noexcept;
    [[nodiscard]] bool DragMaterialGraphBoxSelection(int x, int y) noexcept;
    [[nodiscard]] bool EndMaterialGraphBoxSelection(std::vector<std::uint32_t> nodeIds, std::uint32_t primaryNodeId);
    [[nodiscard]] bool IsMaterialGraphBoxSelecting() const noexcept;
    [[nodiscard]] bool MaterialGraphBoxSelectionAdditive() const noexcept;
    [[nodiscard]] MaterialGraphSelectionOperation MaterialGraphBoxSelectionOperation() const noexcept;
    [[nodiscard]] int MaterialGraphBoxSelectionStartX() const noexcept;
    [[nodiscard]] int MaterialGraphBoxSelectionStartY() const noexcept;
    [[nodiscard]] int MaterialGraphBoxSelectionCurrentX() const noexcept;
    [[nodiscard]] int MaterialGraphBoxSelectionCurrentY() const noexcept;
    [[nodiscard]] bool BeginMaterialGraphPan(int x, int y) noexcept;
    [[nodiscard]] bool DragMaterialGraphPan(int x, int y) noexcept;
    [[nodiscard]] bool EndMaterialGraphPan() noexcept;
    [[nodiscard]] bool IsMaterialGraphPanning() const noexcept;
    // Orbit gesture on the preview overlay: LMB-down begins, mouse-move drags (yaw/pitch), LMB-up ends.
    [[nodiscard]] bool BeginMaterialPreviewOrbit(int x, int y) noexcept;
    [[nodiscard]] bool DragMaterialPreviewOrbit(int x, int y);
    [[nodiscard]] bool EndMaterialPreviewOrbit() noexcept;
    [[nodiscard]] bool IsMaterialPreviewOrbiting() const noexcept;
    [[nodiscard]] bool HasMaterialGraphPanMoved() const noexcept;
    [[nodiscard]] int MaterialGraphNodeOffsetX(kb::assets::AssetId assetId, std::uint32_t nodeId) const noexcept;
    [[nodiscard]] int MaterialGraphNodeOffsetY(kb::assets::AssetId assetId, std::uint32_t nodeId) const noexcept;
    [[nodiscard]] bool AddMaterialGraphNode(
        kb::assets::AssetId id,
        kb::render::RenderMaterialGraphNodeKind kind,
        int graphX,
        int graphY);
    [[nodiscard]] bool AddMaterialGraphComment(kb::assets::AssetId id, int graphX, int graphY);
    [[nodiscard]] bool AddMaterialGraphComposite(kb::assets::AssetId id, int graphX, int graphY);
    [[nodiscard]] bool ExpandMaterialGraphComposite(kb::assets::AssetId id, std::uint32_t compositeId);
    [[nodiscard]] bool DeleteSelectedMaterialGraphNode(kb::assets::AssetId id);
    [[nodiscard]] bool DeleteSelectedMaterialGraphComment(kb::assets::AssetId id);
    [[nodiscard]] bool SetMaterialGraphCommentText(kb::assets::AssetId id, std::uint32_t commentId, std::string_view text);
    [[nodiscard]] bool SetMaterialGraphCommentColor(kb::assets::AssetId id, std::uint32_t commentId, std::uint32_t color);
    [[nodiscard]] bool DisconnectSelectedMaterialGraphNodeLinks(kb::assets::AssetId id);
    [[nodiscard]] bool CopySelectedMaterialGraphNodes();
    [[nodiscard]] bool PasteMaterialGraphNodes(kb::assets::AssetId id, int offsetX, int offsetY);
    [[nodiscard]] bool DuplicateSelectedMaterialGraphNodes(kb::assets::AssetId id, int offsetX, int offsetY);
    [[nodiscard]] bool BeginMaterialGraphWorkingCopyTransaction(kb::assets::AssetId id, std::string label);
    [[nodiscard]] bool CommitMaterialGraphWorkingCopyTransaction();
    [[nodiscard]] bool AbandonMaterialGraphPinConnection();
    [[nodiscard]] bool IsMaterialGraphPinConnectionDetach() const noexcept;
    [[nodiscard]] std::uint64_t MaterialGraphViewSignature(kb::assets::AssetId assetId) const noexcept;
    // Everything the DRAWN graph content depends on (the view signature plus selection, preview-selected node,
    // comment selection, per-node diagnostic markers, and the asset registry generation for texture previews).
    // The panel caches the rendered graph bitmap keyed by this, so overlay-only repaints - navigating the node
    // palette, dragging a selection box or an unconnected wire - blit the cached graph instead of redrawing
    // every node. Deliberately excludes hover: the graph body/pins/links draw identically regardless of hover.
    [[nodiscard]] std::uint64_t MaterialGraphContentDrawSignature(kb::assets::AssetId assetId) const noexcept;
    void CancelMaterialGraphWorkingCopyTransaction();
    [[nodiscard]] bool HasMaterialGraphWorkingCopyTransaction() const noexcept;
    // True while a graph gesture owns the working copy: a comment drag or a pin rewire has already edited the
    // document, or a node drag is holding the "before" snapshot its undo record will use. Editing, undoing or
    // saving during that window corrupts either the file or the history.
    [[nodiscard]] bool HasMaterialGraphGestureInFlight() const noexcept;
    // Ends whatever gesture is in flight the way a mouse-up would: a drag is committed, so its move is
    // recorded, counts as unsaved work and stays undoable; a wire still in mid-air is cancelled, because it
    // was never dropped on a pin. Close/quit paths call this before they read the document, so a prompt or a
    // save never sees a half-finished gesture. Returns whether anything had to be settled.
    bool SettleMaterialGraphGesture();
    [[nodiscard]] bool SetMaterialGraphTextureSampleAsset(kb::assets::AssetId id, std::uint32_t nodeId, kb::assets::AssetId textureId);
    [[nodiscard]] bool SetMaterialGraphConstantColorValue(kb::assets::AssetId id, std::uint32_t nodeId, const std::array<float, 4U>& color);
    [[nodiscard]] bool SetMaterialGraphNodeColorPropertyValue(
        kb::assets::AssetId id,
        std::uint32_t nodeId,
        std::string_view propertyId,
        const std::array<float, 4U>& color);
    [[nodiscard]] bool SetMaterialGraphNodeEnumValue(kb::assets::AssetId id, std::uint32_t nodeId, std::string_view propertyId, std::string_view value);
    void ToggleMaterialGraphNodeEnumDropdown(std::uint32_t nodeId, std::string propertyId);
    void CloseMaterialGraphNodeEnumDropdown() noexcept;
    // Material-level settings (domain / shading model / blend mode). The document owns the values; these
    // write through the same working-copy edit path as everything else, so undo/dirty/cook all apply.
    [[nodiscard]] bool SetMaterialGraphSetting(kb::assets::AssetId id, std::string_view propertyId, std::string_view value);
    void ToggleMaterialGraphSettingDropdown(std::string propertyId);
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
    [[nodiscard]] bool DetachMaterialGraphInputPinConnection(
        kb::assets::AssetId id,
        std::uint32_t toNodeId,
        std::string_view toPin,
        int x,
        int y);
    [[nodiscard]] bool CancelMaterialGraphPinConnection();
    [[nodiscard]] bool CancelMaterialGraphInteractions();
    void ResetMaterialGraphTransientState();
    [[nodiscard]] bool HasMaterialGraphPinConnection() const noexcept;
    [[nodiscard]] kb::assets::AssetId MaterialGraphPinConnectionAssetId() const noexcept;
    [[nodiscard]] std::uint32_t MaterialGraphPinConnectionNodeId() const noexcept;
    [[nodiscard]] std::string_view MaterialGraphPinConnectionPin() const noexcept;
    [[nodiscard]] bool MaterialGraphPinConnectionIsOutput() const noexcept;
    [[nodiscard]] int MaterialGraphPinConnectionX() const noexcept;
    [[nodiscard]] int MaterialGraphPinConnectionY() const noexcept;
    [[nodiscard]] bool OpenMaterialGraphContextMenu(kb::assets::AssetId id, int x, int y, int graphX, int graphY) noexcept;
    [[nodiscard]] bool OpenMaterialGraphContextMenuForPinConnection(kb::assets::AssetId id, int x, int y, int graphX, int graphY) noexcept;
    [[nodiscard]] bool CloseMaterialGraphContextMenu() noexcept;
    [[nodiscard]] bool IsMaterialGraphContextMenuOpen() const noexcept;
    [[nodiscard]] int MaterialGraphContextMenuX() const noexcept;
    [[nodiscard]] int MaterialGraphContextMenuY() const noexcept;
    [[nodiscard]] int MaterialGraphContextMenuGraphX() const noexcept;
    [[nodiscard]] int MaterialGraphContextMenuGraphY() const noexcept;
    [[nodiscard]] int MaterialGraphContextMenuScrollOffset() const noexcept;
    [[nodiscard]] bool SetMaterialGraphContextMenuScrollOffset(int offset, int maxOffset) noexcept;
    [[nodiscard]] bool ScrollMaterialGraphContextMenu(int wheelDelta, int maxOffset) noexcept;
    [[nodiscard]] bool MoveMaterialGraphContextMenuKeyboardSelection(int direction);
    [[nodiscard]] bool ActivateMaterialGraphContextMenuKeyboardSelection();
    [[nodiscard]] std::string_view MaterialGraphContextMenuSearchQuery() const noexcept;
    void SetMaterialGraphContextMenuSearchQuery(std::string query);
    void AppendMaterialGraphContextMenuSearchText(wchar_t character);
    void BackspaceMaterialGraphContextMenuSearch();
    void ClearMaterialGraphContextMenuSearch() noexcept;
    [[nodiscard]] const std::vector<MaterialEditorGraphMenuCommand>& MaterialGraphPaletteFavoriteCommands() const noexcept;
    [[nodiscard]] bool IsMaterialGraphPaletteFavorite(MaterialEditorGraphMenuCommand command) const noexcept;
    [[nodiscard]] bool ToggleMaterialGraphPaletteFavorite(MaterialEditorGraphMenuCommand command);
    [[nodiscard]] bool IsMaterialGraphContextMenuPinFiltered() const noexcept;
    [[nodiscard]] std::uint32_t MaterialGraphContextMenuPinFilterNodeId() const noexcept;
    [[nodiscard]] std::string_view MaterialGraphContextMenuPinFilterPin() const noexcept;
    [[nodiscard]] bool MaterialGraphContextMenuPinFilterIsOutput() const noexcept;
    [[nodiscard]] bool IsMaterialGraphContextMenuCategoryExpanded(std::size_t categoryIndex) const noexcept;
    [[nodiscard]] bool IsMaterialGraphContextMenuCategoryHovered(std::size_t categoryIndex) const noexcept;
    [[nodiscard]] bool IsMaterialGraphContextMenuCommandHovered(std::size_t categoryIndex, MaterialEditorGraphMenuCommand command) const noexcept;
    [[nodiscard]] bool SetMaterialGraphContextMenuHover(std::size_t categoryIndex, MaterialEditorGraphMenuCommand command) noexcept;
    [[nodiscard]] bool ClearMaterialGraphContextMenuHover() noexcept;
    [[nodiscard]] bool ToggleMaterialGraphContextMenuCategory(std::size_t categoryIndex) noexcept;
    [[nodiscard]] bool ExecuteMaterialGraphContextMenuCommand(MaterialEditorGraphMenuCommand command);
    [[nodiscard]] bool OpenMaterialGraphTexturePicker(
        kb::assets::AssetId id,
        std::uint32_t nodeId,
        kb::assets::AssetId currentTexture) noexcept;
    [[nodiscard]] bool CloseMaterialGraphTexturePicker() noexcept;
    [[nodiscard]] bool IsMaterialGraphTexturePickerOpen() const noexcept;
    [[nodiscard]] kb::assets::AssetId MaterialGraphTexturePickerAssetId() const noexcept;
    [[nodiscard]] std::uint32_t MaterialGraphTexturePickerNodeId() const noexcept;
    [[nodiscard]] kb::assets::AssetId MaterialGraphTexturePickerSelectedAssetId() const noexcept;
    [[nodiscard]] bool SetMaterialGraphTexturePickerSelected(kb::assets::AssetId textureId) noexcept;
    [[nodiscard]] std::string_view MaterialGraphTexturePickerSearchQuery() const noexcept;
    void SetMaterialGraphTexturePickerSearchQuery(std::string query);
    void AppendMaterialGraphTexturePickerSearchText(wchar_t character);
    void BackspaceMaterialGraphTexturePickerSearch();
    void ClearMaterialGraphTexturePickerSearch() noexcept;
    [[nodiscard]] int MaterialGraphTexturePickerScrollOffset() const noexcept;
    [[nodiscard]] bool SetMaterialGraphTexturePickerScrollOffset(int offset, int maxOffset) noexcept;
    [[nodiscard]] bool ScrollMaterialGraphTexturePicker(int wheelDelta, int maxOffset) noexcept;
    [[nodiscard]] bool SetMaterialEditorGraphParameterValue(
        kb::assets::AssetId id,
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type,
        std::string_view valueText);
    [[nodiscard]] bool SetMaterialInstanceEditorGraphParameterValue(
        kb::assets::AssetId id,
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type,
        std::string_view valueText);
    [[nodiscard]] bool ClearMaterialInstanceEditorGraphParameterOverride(
        kb::assets::AssetId id,
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type);
    [[nodiscard]] bool SetMaterialInstanceEditorStaticParameterOverride(
        kb::assets::AssetId id,
        std::string_view stableId,
        kb::render::RenderMaterialGraphNodeKind nodeKind,
        std::string value);
    [[nodiscard]] bool SetMaterialInstanceEditorTextureParameterValue(
        kb::assets::AssetId id,
        std::string_view stableId,
        kb::assets::AssetId textureId);
    [[nodiscard]] bool SetMaterialGraphConstantValue(
        kb::assets::AssetId id,
        std::uint32_t nodeId,
        std::string_view valueText);
    [[nodiscard]] bool SetMaterialGraphNodeTextProperty(
        kb::assets::AssetId id,
        std::uint32_t nodeId,
        std::string_view propertyId,
        std::string_view value);
    [[nodiscard]] bool SetMaterialGraphNodeDisplayName(kb::assets::AssetId id, std::uint32_t nodeId, std::string_view displayName);
    [[nodiscard]] bool BeginMaterialGraphNodeRenameEdit(kb::assets::AssetId id, std::uint32_t nodeId);
    [[nodiscard]] bool IsMaterialGraphNodeRenameEditing() const noexcept;
    void AppendMaterialGraphNodeRenameEditText(wchar_t character);
    void InsertMaterialGraphNodeRenameEditText(std::string_view text);
    void BackspaceMaterialGraphNodeRenameEdit();
    void ClearMaterialGraphNodeRenameEditText();
    void SelectAllMaterialGraphNodeRenameEditText() noexcept;
    [[nodiscard]] bool CommitMaterialGraphNodeRenameEdit();
    void CancelMaterialGraphNodeRenameEdit() noexcept;
    [[nodiscard]] bool BeginMaterialGraphConstantInlineEdit(kb::assets::AssetId id, std::uint32_t nodeId);
    [[nodiscard]] bool IsMaterialGraphConstantInlineEditing() const noexcept;
    void AppendMaterialGraphConstantInlineEditText(wchar_t character);
    void BackspaceMaterialGraphConstantInlineEdit();
    [[nodiscard]] bool CommitMaterialGraphConstantInlineEdit();
    void CancelMaterialGraphConstantInlineEdit() noexcept;
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
    [[nodiscard]] std::vector<std::string> ProjectPhysicsLayersAssetOptions() const;
    [[nodiscard]] bool SetProjectInputMappingContext(std::string virtualPath);
    [[nodiscard]] bool ToggleProjectInputEnabled();
    [[nodiscard]] bool SetProjectSceneLightingPath(kb::project::ProjectSceneLightingPath path);
    [[nodiscard]] bool SetProjectPhysicsLayersAsset(std::string virtualPath);
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
    [[nodiscard]] bool SetSkeletonBindingAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool SetDeformedGeometryMeshAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool SetDeformedGeometryMaterialSlotAsset(kb::scene::SceneEntity entity, std::uint32_t slotIndex, kb::assets::AssetId assetId);
    [[nodiscard]] bool RemoveSkeletonBindingFromEntity(kb::scene::SceneEntity entity);
    [[nodiscard]] bool RemoveDeformedGeometryFromEntity(kb::scene::SceneEntity entity);

    // Inspector-driven script behaviour authoring.
    [[nodiscard]] bool HasEntityScript(kb::scene::SceneEntity entity) const;
    [[nodiscard]] std::string EntityScriptName(kb::scene::SceneEntity entity) const;
    [[nodiscard]] bool EntityScriptEnabled(kb::scene::SceneEntity entity) const;
    [[nodiscard]] std::vector<std::pair<kb::assets::AssetId, std::string>> AvailableScriptAssets() const;
    [[nodiscard]] bool AttachScriptToEntity(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool RemoveScriptFromEntity(kb::scene::SceneEntity entity);
    // Removes the Mesh Renderer component (the section-header "×"), mirroring the
    // Script remove path. Undoable; false if the entity has no Mesh Renderer.
    [[nodiscard]] bool RemoveMeshRendererFromEntity(kb::scene::SceneEntity entity);
    // Removes one physics component (Rigidbody/Collider/CharacterController/Joint)
    // via its section-header "×". Undoable; false if the entity lacks that one.
    [[nodiscard]] bool RemovePhysicsComponent(kb::scene::SceneEntity entity, PhysicsComponentKind kind);
    // Whether the green collider wireframes are drawn in the Scene Viewport (the
    // Unity-style "Gizmos" toggle). Default on so colliders are visible.
    [[nodiscard]] bool ArePhysicsGizmosVisible() const noexcept { return physicsGizmosVisible_; }
    void SetPhysicsGizmosVisible(bool visible) noexcept { physicsGizmosVisible_ = visible; }
    // Sizes the entity's Collider to enclose its Mesh Renderer mesh (Unity-style
    // "Fit to Mesh"): center + radius/box-size/height from the mesh bounds.
    // Undoable; false if the entity has no Collider or no loadable mesh.
    [[nodiscard]] bool FitColliderToMesh(kb::scene::SceneEntity entity);
    // True when the entity has both a Collider and a resolvable Mesh Renderer mesh
    // (so the "Fit to Mesh" affordance is meaningful).
    [[nodiscard]] bool CanFitColliderToMesh(kb::scene::SceneEntity entity) const;
    // Non-transactional core of the collider fit: reads the entity's mesh bounds
    // and writes them into its Collider in place. Returns false (and leaves the
    // collider untouched) when no bounds are resolvable. Shared by the "Fit to
    // Mesh" action, collider-add auto-fit, and mesh-change auto-refit — each of
    // which supplies its own undoable transaction. `reason` receives a
    // human-readable outcome for the Console.
    bool ApplyColliderFitToMesh(kb::scene::SceneEntity entity, std::string& reason);
    // The Lua behaviour asset bound to the entity (for the Script field's picker
    // "reveal in Project Files"); invalid AssetId when no script is attached.
    [[nodiscard]] kb::assets::AssetId EntityScriptAssetId(kb::scene::SceneEntity entity) const;
    [[nodiscard]] bool ToggleEntityScriptEnabled(kb::scene::SceneEntity entity);
    // One exposed ("@expose") script variable as the Inspector shows it: the
    // declared name/type, the EFFECTIVE value (the per-instance override if one
    // is stored, else the script's declared default), and whether an override
    // delta currently exists for it.
    struct EntityScriptVariable {
        std::string name;
        kb::script::ScriptValueType type = kb::script::ScriptValueType::Void;
        kb::script::ScriptValue value;
        bool overridden = false;
    };
    [[nodiscard]] std::vector<EntityScriptVariable> EntityScriptExposedVariables(kb::scene::SceneEntity entity) const;
    // Authors a per-instance override; if the value equals the script's declared
    // default the override is dropped instead (store-only-non-default). Undoable.
    [[nodiscard]] bool SetEntityScriptVariable(kb::scene::SceneEntity entity, std::string name, kb::script::ScriptValue value);
    // Drops the override for one variable (revert to its @expose default). Undoable.
    [[nodiscard]] bool RevertEntityScriptVariable(kb::scene::SceneEntity entity, std::string_view name);
    // Drops the currently-open script's cached asset so its next load re-parses
    // from disk — the exposed-variable schema then reflects edits the user just
    // saved in the Script Editor. Returns true when an open script was unloaded
    // (i.e. any Inspector showing it should repaint). Not undoable (pure cache).
    [[nodiscard]] bool ReloadOpenScriptAsset();
    [[nodiscard]] bool AddComponentToEntity(kb::scene::SceneEntity entity, std::string_view componentId);
    [[nodiscard]] std::vector<std::string> EntityTags(kb::scene::SceneEntity entity) const;
    [[nodiscard]] std::vector<std::string> KnownSceneTags() const;
    [[nodiscard]] bool SetEntityTagSelected(kb::scene::SceneEntity entity, std::string_view tag, bool selected);
    [[nodiscard]] bool RemoveTagsFromEntity(kb::scene::SceneEntity entity);
    [[nodiscard]] bool DeleteSceneTag(std::string_view tag);
    [[nodiscard]] bool SetAudioSourceClipAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool SetAnimatorControllerAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool ToggleSkeletonBindingEnabled(kb::scene::SceneEntity entity);
    [[nodiscard]] bool ToggleDeformedGeometryEnabled(kb::scene::SceneEntity entity);
    [[nodiscard]] bool ToggleDeformedGeometryCastsShadow(kb::scene::SceneEntity entity);
    [[nodiscard]] bool ToggleDeformedGeometryReceivesShadow(kb::scene::SceneEntity entity);
    [[nodiscard]] bool SetUIDocumentAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId);
    [[nodiscard]] bool SetAnimatorSpeed(kb::scene::SceneEntity entity, float speed);
    [[nodiscard]] bool ToggleAnimatorEnabled(kb::scene::SceneEntity entity);
    [[nodiscard]] bool CycleAnimatorRootMotionOwner(kb::scene::SceneEntity entity);
    [[nodiscard]] bool RemoveAnimatorFromEntity(kb::scene::SceneEntity entity);
    [[nodiscard]] bool ToggleUIDocumentEnabled(kb::scene::SceneEntity entity);
    [[nodiscard]] bool RemoveUIDocumentFromEntity(kb::scene::SceneEntity entity);
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
    [[nodiscard]] bool BeginTerrainBrushStroke(
        kb::scene::SceneEntity entity,
        std::string label,
        bool layerPaint,
        std::string* error);
    [[nodiscard]] bool FinalizeActiveTransformEditApply(
        bool changed,
        std::span<const kb::scene::SceneEntity> touched);
    [[nodiscard]] bool ExecuteSceneCommand(std::string label, std::function<bool()> mutation);
    [[nodiscard]] bool ExecuteMaterialAssetEdit(kb::assets::AssetId id, std::unique_ptr<IEditorMaterialAssetPropertyEdit> edit);
    [[nodiscard]] bool RecordMaterialGraphWorkingCopyEdit(
        kb::assets::AssetId id,
        std::string label,
        kb::render::RenderMaterialAssetData before,
        std::uint32_t beforeSelectedNodeId,
        std::vector<std::uint32_t> beforeSelectedNodeIds = {},
        std::uint32_t beforeSelectedCommentId = 0U);
    void ClearMaterialGraphWorkingCopyTransaction() noexcept;
    void ClearMaterialGraphPinConnectionState() noexcept;
    [[nodiscard]] bool AddMaterialGraphNodeForPendingConnection(kb::assets::AssetId id, MaterialEditorGraphMenuCommand command, int graphX, int graphY);
    [[nodiscard]] std::optional<kb::render::RenderMaterialAssetData> MaterialSourceForEdit(kb::assets::AssetId id) const;
    // A material graph edit only changes ONE material asset; a full MarkSceneRenderDirty() forces a full
    // resync of every mesh in the scene (a multi-second stall in Debug on any non-trivial scene). This marks
    // dirty only the scene's mesh-renderer entities that actually reference `id` (primary slot or override),
    // via the same incremental MarkSceneEntitiesRenderDirty path transform/object edits already use - and
    // does nothing at all (no revision bump, no resync) when the edited material isn't equipped on any scene
    // mesh, which is the common case while authoring a graph in the Material Editor's own preview.
    void MarkMaterialAssetRenderDirty(kb::assets::AssetId id);
    [[nodiscard]] bool ApplyPatchToMaterialEditorWorkingCopy(kb::assets::AssetId id, IEditorMaterialAssetPropertyEdit& edit);
    [[nodiscard]] bool CopyWorkingMaterialToSource(kb::assets::AssetId id);
    [[nodiscard]] bool CopyWorkingMaterialInstanceToSource(kb::assets::AssetId id);
    [[nodiscard]] bool ValidateMaterialEditorAssetCandidate(
        kb::assets::AssetId id,
        const kb::render::RenderMaterialAssetData* materialCandidate,
        const kb::render::RenderMaterialInstanceAssetData* instanceCandidate);
    void SyncMaterialEditorWorkingCopyRuntimePreview();
    void ClearMaterialEditorWorkingCopyRuntimePreview();
    // MAT-84: cook every graph-backed material referenced by scene meshes (even unopened ones) so
    // the scene/game render their real GPU graph program instead of the CPU fallback.
    void CookSceneGraphMaterials();
    void RequestOpenMaterialSceneGraphCook();
    void RefreshOpenMaterialEditorFromSource();
    [[nodiscard]] EditorInputActionAuthoring InputActionAuthoring() noexcept;
    [[nodiscard]] EditorInputMappingContextAuthoring InputMappingContextAuthoring() noexcept;
    [[nodiscard]] EditorMaterialAssetAuthoring MaterialAssetAuthoring() noexcept;
    void ActivateProjectInput();
    [[nodiscard]] bool ActivateProjectPhysicsLayers(kb::scene::Scene& scene);
    void EnsureScriptRuntime();
    void SurfaceScriptLibraryStartupReport();
    void ResetScriptRuntimeStateForPlayMode();
    [[nodiscard]] bool SaveProjectDescriptor();
    void ClearSceneDocumentDirty() noexcept;
    void ReleaseRenderedSceneResources();
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
    std::function<void(const kb::scene::Scene&)> renderSceneReleaseHandler_;
    std::filesystem::path currentScenePath_;
    EditorAssetBrowserState assetBrowser_;
    EditorConsoleState console_;
    EditorSceneViewportStateStore viewportState_;
    AnimationPreviewContext animationPreview_;
    InspectorPanelState inspector_;
    MaterialEditorState materialEditor_;
    kb::assets::AssetId materialRuntimePreviewAssetId_{};
    std::optional<kb::assets::AssetMetadata> materialRuntimePreviewSourceMetadata_;
    std::filesystem::path materialRuntimePreviewPath_;
    std::uint64_t materialRuntimePreviewContentHash_ = 0U;
    std::optional<kb::render::RenderMaterialAssetData> materialNodePreviewWorkingCopy_;
    bool materialPreviewNodePreviewEnabled_ = false;
    EditorProjectSettingsState projectSettings_;
    EditorPluginsState plugins_;
    EditorScriptEditorState scriptEditor_;
    bool physicsGizmosVisible_ = true;
    // Inspector and Material Editor can display different assets in the same paint batch. Each queued
    // viewport keeps a raw Scene pointer until EndPaintLayout(), so sharing one mutable preview scene here
    // would let the second surface destroy the first surface's queued Scene (use-after-free).
    std::unique_ptr<EditorMaterialPreviewScene> inspectorMaterialPreviewScene_;
    std::unique_ptr<EditorMaterialPreviewScene> materialPreviewScene_;
    // Thumbnails own a third preview scene so their asynchronous capture channel and per-asset rebuilds
    // cannot alter either visible preview surface.
    std::unique_ptr<EditorMaterialPreviewScene> materialThumbnailScene_;
    std::string graphShaderCacheRoot_;
    std::unique_ptr<EditorMaterialGraphCookService> materialGraphCookService_;
    bool sceneGraphCookPending_ = true;
    EditorCommandStack commandStack_;
    std::optional<TerrainStrokeState> terrainStroke_;
    mutable std::optional<TerrainReadCache> terrainReadCache_;
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
    struct MaterialGraphViewState {
        float zoom = MaterialGraphInteractionPolicy::DefaultZoom;
        int panX = 0;
        int panY = 0;
    };
    std::unordered_map<std::uint64_t, MaterialGraphViewState> materialGraphViewStates_;
    float materialGraphZoom_ = MaterialGraphInteractionPolicy::DefaultZoom;
    int materialGraphPanX_ = 0;
    int materialGraphPanY_ = 0;
    int materialGraphCanvasWidth_ = 1280;
    int materialGraphCanvasHeight_ = 720;
    int materialGraphCanvasLeft_ = 0;
    int materialGraphCanvasTop_ = 0;
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
    std::vector<std::uint32_t> materialGraphDragStartSelectedNodeIds_;
    std::vector<MaterialGraphDragNodeStart> materialGraphDragStartNodes_;
    bool materialGraphDragChanged_ = false;
    bool materialGraphNodeDragging_ = false;
    kb::assets::AssetId materialGraphCommentDragAssetId_{};
    std::uint32_t materialGraphCommentDragId_ = 0U;
    int materialGraphCommentDragStartX_ = 0;
    int materialGraphCommentDragStartY_ = 0;
    int materialGraphCommentDragStartCommentX_ = 0;
    int materialGraphCommentDragStartCommentY_ = 0;
    std::optional<kb::render::RenderMaterialAssetData> materialGraphCommentDragStartDocument_;
    std::uint32_t materialGraphCommentDragStartSelectedNodeId_ = 0U;
    std::vector<std::uint32_t> materialGraphCommentDragStartSelectedNodeIds_;
    std::vector<std::uint32_t> materialGraphCommentDragMemberNodeIds_;
    std::uint32_t materialGraphCommentDragStartSelectedCommentId_ = 0U;
    bool materialGraphCommentDragChanged_ = false;
    bool materialGraphCommentDragging_ = false;
    kb::assets::AssetId materialGraphBoxSelectionAssetId_{};
    int materialGraphBoxSelectionStartX_ = 0;
    int materialGraphBoxSelectionStartY_ = 0;
    int materialGraphBoxSelectionCurrentX_ = 0;
    int materialGraphBoxSelectionCurrentY_ = 0;
    MaterialGraphSelectionOperation materialGraphBoxSelectionOperation_ = MaterialGraphSelectionOperation::Replace;
    std::vector<std::uint32_t> materialGraphBoxSelectionBaseNodeIds_;
    std::uint32_t materialGraphBoxSelectionBasePrimaryNodeId_ = 0U;
    bool materialGraphBoxSelectionMoved_ = false;
    bool materialGraphBoxSelecting_ = false;
    bool materialGraphFocused_ = false;
    int materialEditorDetailsScrollOffset_ = 0;
    int materialGraphPanStartX_ = 0;
    int materialGraphPanStartY_ = 0;
    int materialGraphPanStartOffsetX_ = 0;
    int materialGraphPanStartOffsetY_ = 0;
    bool materialGraphPanning_ = false;
    bool materialGraphPanMoved_ = false;
    bool materialPreviewOrbitDragging_ = false;
    int materialPreviewOrbitLastX_ = 0;
    int materialPreviewOrbitLastY_ = 0;
    kb::assets::AssetId materialGraphPendingConnectionAssetId_{};
    std::uint32_t materialGraphPendingConnectionNodeId_ = 0U;
    std::string materialGraphPendingConnectionPin_;
    bool materialGraphPendingConnectionOutput_ = true;
    int materialGraphPendingConnectionX_ = 0;
    int materialGraphPendingConnectionY_ = 0;
    bool materialGraphPendingConnectionOwnsTransaction_ = false;
    kb::assets::AssetId materialGraphContextMenuAssetId_{};
    int materialGraphContextMenuX_ = 0;
    int materialGraphContextMenuY_ = 0;
    int materialGraphContextMenuGraphX_ = 0;
    int materialGraphContextMenuGraphY_ = 0;
    int materialGraphContextMenuScrollOffset_ = 0;
    std::uint32_t materialGraphContextMenuExpandedMask_ = 0U;
    std::size_t materialGraphContextMenuHoveredCategory_ = static_cast<std::size_t>(-1);
    MaterialEditorGraphMenuCommand materialGraphContextMenuHoveredCommand_ = MaterialEditorGraphMenuCommand::None;
    std::string materialGraphContextMenuSearchQuery_;
    std::vector<MaterialEditorGraphMenuCommand> materialGraphPaletteFavorites_;
    std::uint32_t materialGraphContextMenuPinFilterNodeId_ = 0U;
    std::string materialGraphContextMenuPinFilterPin_;
    bool materialGraphContextMenuPinFilterOutput_ = true;
    bool materialGraphContextMenuPinFilterActive_ = false;
    kb::assets::AssetId materialGraphTexturePickerAssetId_{};
    std::uint32_t materialGraphTexturePickerNodeId_ = 0U;
    kb::assets::AssetId materialGraphTexturePickerSelectedTextureId_{};
    std::string materialGraphTexturePickerSearchQuery_;
    int materialGraphTexturePickerScrollOffset_ = 0;
    kb::assets::AssetId materialGraphWorkingCopyTransactionAssetId_{};
    std::string materialGraphWorkingCopyTransactionLabel_;
    std::optional<kb::render::RenderMaterialAssetData> materialGraphWorkingCopyTransactionBefore_;
    std::uint32_t materialGraphWorkingCopyTransactionBeforeSelectedNodeId_ = 0U;
    std::vector<std::uint32_t> materialGraphWorkingCopyTransactionBeforeSelectedNodeIds_;
    std::uint32_t materialGraphWorkingCopyTransactionBeforeSelectedCommentId_ = 0U;
    bool materialGraphWorkingCopyTransactionChanged_ = false;
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
    // EditorSceneContext's destructor destroys scene_ explicitly before
    // resetting this host, while console_ and the host are still alive, because
    // script Destroyed callbacks may log during scene shutdown.
    kb::script::ScriptModule* scriptModule_ = nullptr;
    std::unique_ptr<kb::modules::EngineModuleHost> scriptModuleHost_;
};

} // namespace kb::editor
