#include "app/EditorPointerDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorAssetFolderProjectFilesDropHandler.hpp"
#include "app/EditorAudioAssetInspectorDropHandler.hpp"
#include "app/EditorBehaviourAssetHierarchyDropHandler.hpp"
#include "app/EditorBehaviourAssetInspectorDropHandler.hpp"
#include "app/EditorBehaviourAssetSceneDropHandler.hpp"
#include "app/EditorDropPanelResolver.hpp"
#include "app/EditorHierarchyEntityAssetDropHandler.hpp"
#include "app/EditorHierarchyEntityHierarchyDropHandler.hpp"
#include "app/EditorMaterialAssetInspectorDropHandler.hpp"
#include "app/EditorMaterialAssetSceneDropHandler.hpp"
#include "app/EditorMeshAssetSceneDropHandler.hpp"
#include "app/EditorPrefabAssetHierarchyDropHandler.hpp"
#include "app/EditorPrefabAssetProjectFilesDropHandler.hpp"
#include "app/EditorPrefabAssetSceneDropHandler.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
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
    const std::optional<kb::render::RenderMaterialAssetData> materialAsset = sceneContext.ReadMaterialAsset(materialId);
    if (!materialAsset.has_value()) {
        return false;
    }

    const std::optional<EditorMaterialTextureSlot> slot = MaterialEditorPanelRenderer::TextureSlotAt(*materialEditor, materialAsset->graph, sceneContext, materialId, x, y);
    if (!slot.has_value()) {
        return false;
    }
    return sceneContext.SetMaterialTextureAsset(materialId, *slot, textureId);
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
    switch (drag.kind) {
    case EditorPointerDragKind::HierarchyEntity:
        return EditorHierarchyEntityAssetDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.entity)
            || EditorHierarchyEntityHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.entities);
    case EditorPointerDragKind::PrefabAsset:
        return (drag.assetInstantiatesPrefab && EditorPrefabAssetSceneDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetPath, drag.assetVirtualPath))
            || (drag.assetInstantiatesPrefab && EditorPrefabAssetHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetPath, drag.assetVirtualPath))
            || (drag.assetCreatesMeshEntity && EditorMeshAssetSceneDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAddsBehaviour && EditorBehaviourAssetHierarchyDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAddsBehaviour && EditorBehaviourAssetSceneDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAddsBehaviour && EditorBehaviourAssetInspectorDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAssignsAudioClip && EditorAudioAssetInspectorDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetAssignsTexture && DropTextureOnMaterialEditor(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetId.IsValid() && EditorMaterialAssetSceneDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || (drag.assetId.IsValid() && EditorMaterialAssetInspectorDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId))
            || EditorPrefabAssetProjectFilesDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetId);
    case EditorPointerDragKind::AssetFolder:
        return EditorAssetFolderProjectFilesDropHandler::Drop(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext, drag.assetFolderPath);
    case EditorPointerDragKind::None:
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
