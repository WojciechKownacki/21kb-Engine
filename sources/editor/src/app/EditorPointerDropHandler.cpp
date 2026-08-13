#include "app/EditorPointerDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorAssetFolderProjectFilesDropHandler.hpp"
#include "app/EditorAudioAssetInspectorDropHandler.hpp"
#include "app/EditorAnimatorAssetInspectorDropHandler.hpp"
#include "app/EditorBehaviourAssetHierarchyDropHandler.hpp"
#include "app/EditorBehaviourAssetInspectorDropHandler.hpp"
#include "app/EditorBehaviourAssetSceneDropHandler.hpp"
#include "app/EditorDropPanelResolver.hpp"
#include "app/EditorHierarchyEntityAssetDropHandler.hpp"
#include "app/EditorHierarchyEntityHierarchyDropHandler.hpp"
#include "app/EditorMaterialAssetInspectorDropHandler.hpp"
#include "app/EditorMaterialAssetSceneDropHandler.hpp"
#include "app/EditorMeshAssetInspectorDropHandler.hpp"
#include "app/EditorMeshAssetSceneDropHandler.hpp"
#include "app/EditorPrefabAssetHierarchyDropHandler.hpp"
#include "app/EditorPrefabAssetProjectFilesDropHandler.hpp"
#include "app/EditorPrefabAssetSceneDropHandler.hpp"
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "app/scene_viewport/EditorSceneViewportMeshPicker.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "platform/win32/EditorMaterialAssetPickerDialog.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] EditorTextureAssetPickerFilter TexturePickerFilterForNodeKind(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectCube:
        return EditorTextureAssetPickerFilter::TextureCube;
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume:
        return EditorTextureAssetPickerFilter::TextureVolume;
    case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
    case kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray:
        return EditorTextureAssetPickerFilter::Texture2DArray;
    default:
        return EditorTextureAssetPickerFilter::Texture2D;
    }
}

[[nodiscard]] bool DropTextureOnMaterialEditor(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId textureId) {
    const std::optional<RECT> materialEditor = EditorDropPanelResolver::Resolve(DockPanelKind::MaterialEditor, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!materialEditor.has_value() || !Contains(*materialEditor, x, y)) {
        return false;
    }

    const kb::assets::AssetId materialId = sceneContext.MaterialEditor().OpenAssetId();
    if (!materialId.IsValid()) {
        return false;
    }
    const kb::assets::AssetMetadata* material = sceneContext.Scene().Assets().Manager().Registry().Find(materialId);
    if (material == nullptr || material->type != "RenderMaterial") {
        return false;
    }
    const std::optional<kb::render::RenderMaterialAssetData> materialAsset =
        sceneContext.MaterialEditor().WorkingCopy().has_value()
            ? sceneContext.MaterialEditor().WorkingCopy()
            : sceneContext.ReadMaterialAsset(materialId);
    if (!materialAsset.has_value()) {
        return false;
    }

    if (const std::optional<std::uint32_t> textureNodeId =
            MaterialEditorPanelRenderer::GraphTextureSampleAt(*materialEditor, materialAsset->graph, sceneContext, materialId, x, y)) {
        const kb::render::RenderMaterialGraphNode* textureNode = kb::render::FindRenderMaterialGraphNode(materialAsset->graph, *textureNodeId);
        const kb::assets::AssetMetadata* texture = sceneContext.Scene().Assets().Manager().Registry().Find(textureId);
        if (texture == nullptr) {
            return false;
        }
        const EditorTextureAssetPickerFilter filter = textureNode == nullptr
            ? EditorTextureAssetPickerFilter::Texture2D
            : TexturePickerFilterForNodeKind(textureNode->kind);
        if (!EditorTextureAssetPickerDialog::MatchesFilter(*texture, filter)) {
            sceneContext.Console().Error("Materials", "Texture asset type does not match this material graph node.");
            return true;
        }
        return sceneContext.SetMaterialGraphTextureSampleAsset(materialId, *textureNodeId, textureId);
    }

    const std::optional<EditorMaterialTextureSlot> slot = MaterialEditorPanelRenderer::TextureSlotAt(*materialEditor, materialAsset->graph, sceneContext, materialId, x, y);
    if (!slot.has_value()) {
        return false;
    }
    return sceneContext.SetMaterialTextureAsset(materialId, *slot, textureId);
}

[[nodiscard]] bool DropMeshOnMaterialEditorPreview(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId meshId) {
    const std::optional<RECT> materialEditor =
        EditorDropPanelResolver::Resolve(DockPanelKind::MaterialEditor, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!materialEditor.has_value() || !Contains(*materialEditor, x, y)) {
        return false;
    }
    const std::optional<RECT> preview = MaterialEditorPanelRenderer::MaterialPreviewRect(*materialEditor, sceneContext);
    if (!preview.has_value() || !Contains(*preview, x, y)) {
        return false;
    }

    const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(meshId);
    if (metadata == nullptr || !EditorSceneMeshAssetActions::IsMeshAsset(*metadata)) {
        sceneContext.Console().Error("Materials", "Only mesh assets can be used as the Material Preview mesh.");
        return true;
    }
    static_cast<void>(sceneContext.SetMaterialPreviewPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy::CustomMesh(meshId)));
    sceneContext.Console().Info("Materials", "Material Preview mesh set to " + (metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name) + ".");
    return true;
}

[[nodiscard]] bool DropMaterialGraphPaletteCommand(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId materialId,
    MaterialEditorGraphMenuCommand command) {
    if (!materialId.IsValid() ||
        sceneContext.MaterialEditor().OpenAssetId() != materialId ||
        !MaterialEditorGraphMenuCommandCreatesCanvasObject(command)) {
        return false;
    }

    const std::optional<RECT> materialEditor =
        EditorDropPanelResolver::Resolve(DockPanelKind::MaterialEditor, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!materialEditor.has_value() || !Contains(*materialEditor, x, y)) {
        static_cast<void>(sceneContext.CloseMaterialGraphContextMenu());
        return false;
    }

    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditor);
    if (!MaterialEditorPanelPointInRect(layout.graphCanvas, x, y)) {
        static_cast<void>(sceneContext.CloseMaterialGraphContextMenu());
        return false;
    }

    sceneContext.SetMaterialGraphCanvasViewport(
        layout.graphCanvas.left,
        layout.graphCanvas.top,
        MaterialEditorPanelRectWidth(layout.graphCanvas),
        MaterialEditorPanelRectHeight(layout.graphCanvas));
    const float zoom = std::max(0.1F, sceneContext.MaterialGraphZoom());
    const int graphX = static_cast<int>(static_cast<float>(x - layout.graphCanvas.left - sceneContext.MaterialGraphPanX()) / zoom);
    const int graphY = static_cast<int>(static_cast<float>(y - layout.graphCanvas.top - sceneContext.MaterialGraphPanY()) / zoom);
    if (sceneContext.IsMaterialGraphContextMenuPinFiltered()) {
        if (!sceneContext.OpenMaterialGraphContextMenuForPinConnection(materialId, x, y, graphX, graphY)) {
            static_cast<void>(sceneContext.CancelMaterialGraphPinConnection());
            return false;
        }
        return sceneContext.ExecuteMaterialGraphContextMenuCommand(command);
    }
    static_cast<void>(sceneContext.OpenMaterialGraphContextMenu(materialId, x, y, graphX, graphY));
    return sceneContext.ExecuteMaterialGraphContextMenuCommand(command);
}

[[nodiscard]] bool IsMeshRendererMaterialHit(const InspectorPanelRenderer::Hit& hit) noexcept {
    if (hit.section != InspectorSectionId::MeshRenderer) {
        return false;
    }
    switch (hit.property) {
    case InspectorPropertyId::MeshRendererMaterial:
    case InspectorPropertyId::MeshRendererMaterialPicker:
    case InspectorPropertyId::MeshRendererMaterialOverridePicker:
    case InspectorPropertyId::MeshRendererMaterialSlot0:
    case InspectorPropertyId::MeshRendererMaterialSlot1:
    case InspectorPropertyId::MeshRendererMaterialSlot2:
    case InspectorPropertyId::MeshRendererMaterialSlot3:
    case InspectorPropertyId::MeshRendererMaterialSlot4:
    case InspectorPropertyId::MeshRendererMaterialSlot5:
    case InspectorPropertyId::MeshRendererMaterialSlot6:
    case InspectorPropertyId::MeshRendererMaterialSlot7:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker0:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker1:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker2:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker3:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker4:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker5:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker6:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker7:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::optional<std::uint32_t> MeshRendererMaterialSlotIndex(const InspectorPanelRenderer::Hit& hit) noexcept {
    switch (hit.property) {
    case InspectorPropertyId::MeshRendererMaterialOverridePicker:
    case InspectorPropertyId::MeshRendererMaterialSlot0:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker0:
        return 0U;
    case InspectorPropertyId::MeshRendererMaterialSlot1:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker1:
        return 1U;
    case InspectorPropertyId::MeshRendererMaterialSlot2:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker2:
        return 2U;
    case InspectorPropertyId::MeshRendererMaterialSlot3:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker3:
        return 3U;
    case InspectorPropertyId::MeshRendererMaterialSlot4:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker4:
        return 4U;
    case InspectorPropertyId::MeshRendererMaterialSlot5:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker5:
        return 5U;
    case InspectorPropertyId::MeshRendererMaterialSlot6:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker6:
        return 6U;
    case InspectorPropertyId::MeshRendererMaterialSlot7:
    case InspectorPropertyId::MeshRendererMaterialSlotPicker7:
        return 7U;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<kb::assets::AssetId> CreateMaterialFromGraphForDrop(
    EditorSceneContext& sceneContext,
    kb::assets::AssetId graphAssetId) {
    if (!sceneContext.CreateMaterialFromGraphAsset(graphAssetId)) {
        return std::nullopt;
    }
    const kb::assets::AssetId materialId = sceneContext.AssetBrowser().SelectedAsset();
    const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(materialId);
    if (metadata == nullptr || !EditorSceneMaterialAssetActions::IsMaterialAsset(*metadata)) {
        sceneContext.Console().Error("Materials", "Material Graph drop created no assignable .kbmat material.");
        return std::nullopt;
    }
    return materialId;
}

[[nodiscard]] bool CreateMaterialFromGraphAndAssignToTarget(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId graphAssetId) {
    if (const std::optional<RECT> inspector = EditorDropPanelResolver::Resolve(DockPanelKind::Inspector, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
        inspector.has_value() && Contains(*inspector, x, y)) {
        const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
        if (entity.IsValid() && sceneContext.Scene().Entities().IsAlive(entity)) {
            const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(*inspector, sceneContext, x, y);
            if (IsMeshRendererMaterialHit(hit)) {
                const std::optional<kb::assets::AssetId> materialId = CreateMaterialFromGraphForDrop(sceneContext, graphAssetId);
                if (!materialId.has_value()) {
                    return true;
                }
                if (const std::optional<std::uint32_t> slotIndex = MeshRendererMaterialSlotIndex(hit)) {
                    static_cast<void>(sceneContext.SetMeshRendererMaterialSlotAsset(entity, *slotIndex, *materialId));
                } else {
                    static_cast<void>(sceneContext.SetMeshRendererMaterialAsset(entity, *materialId));
                }
                sceneContext.Console().Info("Materials", "Material Graph converted to .kbmat and assigned to Mesh Renderer.");
                return true;
            }
        }
    }

    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }
    const EditorSceneViewportPickResult pick = EditorSceneViewportMeshPicker::PickNearest(sceneContext.Scene(), hit->ray);
    if (!pick.IsValid()) {
        return false;
    }
    const std::optional<kb::assets::AssetId> materialId = CreateMaterialFromGraphForDrop(sceneContext, graphAssetId);
    if (materialId.has_value()) {
        static_cast<void>(sceneContext.SetMeshRendererMaterialAsset(pick.entity, *materialId));
        sceneContext.SelectEntity(pick.entity);
        sceneContext.Console().Info("Materials", "Material Graph converted to .kbmat and assigned to Mesh Renderer.");
    }
    return true;
}

} // namespace

bool EditorPointerDropHandler::Drop(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    const EditorPointerDragState& drag) {
    if (drag.kind == EditorPointerDragKind::PrefabAsset && drag.assetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(drag.assetId);
        const std::optional<RECT> animator = EditorDropPanelResolver::Resolve(
            DockPanelKind::AnimatorEditor, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
        if (metadata != nullptr && metadata->type == kb::scene::kAnimationClipAssetType && animator.has_value() && Contains(*animator, x, y)) {
            return sceneContext.AddAnimationClipToAnimatorEditor(drag.assetId, x - animator->left, y - animator->top);
        }
    }
    switch (drag.kind) {
    case EditorPointerDragKind::HierarchyEntity:
        return EditorHierarchyEntityAssetDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.entity)
            || EditorHierarchyEntityHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.entities);
    case EditorPointerDragKind::PrefabAsset:
        return (drag.assetId.IsValid() && EditorMeshAssetInspectorDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetInstantiatesPrefab && EditorPrefabAssetSceneDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetPath, drag.assetVirtualPath))
            || (drag.assetInstantiatesPrefab && EditorPrefabAssetHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetPath, drag.assetVirtualPath))
            || (drag.assetCreatesMeshEntity && DropMeshOnMaterialEditorPreview(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetCreatesMeshEntity && EditorMeshAssetSceneDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAddsBehaviour && EditorBehaviourAssetHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAddsBehaviour && EditorBehaviourAssetSceneDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAddsBehaviour && EditorBehaviourAssetInspectorDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || ((drag.assetAssignsAudioClip || drag.assetAssignsAudioMixer)
                && EditorAudioAssetInspectorDropHandler::Drop(
                    sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || EditorAnimatorAssetInspectorDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId)
            || (drag.assetAssignsTexture && DropTextureOnMaterialEditor(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAssignsMaterial && EditorMaterialAssetSceneDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAssignsMaterial && EditorMaterialAssetInspectorDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAssignsMaterialGraph && CreateMaterialFromGraphAndAssignToTarget(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || EditorPrefabAssetProjectFilesDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId);
    case EditorPointerDragKind::AssetFolder:
        return EditorAssetFolderProjectFilesDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetFolderPath);
    case EditorPointerDragKind::MaterialGraphPaletteCommand:
        return DropMaterialGraphPaletteCommand(
            sourceWindow,
            mainWindow,
            x,
            y,
            dockModel,
            floatingWindows,
            metrics,
            sceneContext,
            drag.materialGraphAssetId,
            drag.materialGraphCommand);
    case EditorPointerDragKind::None:
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
