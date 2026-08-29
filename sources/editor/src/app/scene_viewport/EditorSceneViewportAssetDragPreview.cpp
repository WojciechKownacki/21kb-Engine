#include "app/scene_viewport/EditorSceneViewportAssetDragPreview.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] bool PreviewAlive(const EditorSceneContext& sceneContext, const EditorPointerDragState& drag) noexcept {
    return drag.scenePlacementPreview.IsValid() &&
        sceneContext.Scene().Entities().IsAlive(drag.scenePlacementPreview);
}

void CapturePreviousSelection(EditorSceneContext& sceneContext, EditorPointerDragState& drag) {
    if (drag.scenePlacementPreviousSelectionCaptured) {
        return;
    }
    drag.scenePlacementPreviousSelection = sceneContext.SelectedHierarchyEntities();
    drag.scenePlacementPreviousAssetSelection = sceneContext.AssetBrowser().SelectedAsset();
    drag.scenePlacementPreviousSelectionCaptured = true;
}

void RestorePreviousSelection(EditorSceneContext& sceneContext, const EditorPointerDragState& drag) {
    std::vector<kb::scene::SceneEntity> alive;
    alive.reserve(drag.scenePlacementPreviousSelection.size());
    for (const kb::scene::SceneEntity entity : drag.scenePlacementPreviousSelection) {
        if (sceneContext.Scene().Entities().IsAlive(entity)) {
            alive.push_back(entity);
        }
    }
    if (alive.empty()) {
        sceneContext.ClearHierarchySelection();
    } else {
        sceneContext.SelectHierarchyEntities(alive);
    }
    if (drag.scenePlacementPreviousAssetSelection.IsValid()) {
        static_cast<void>(sceneContext.AssetBrowser().SelectAsset(
            drag.scenePlacementPreviousAssetSelection,
            sceneContext.Scene().Assets().Manager()));
    }
}

[[nodiscard]] kb::scene::SceneEntity CreatePreview(
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag,
    kb::scene::Vec3 position) {
    CapturePreviousSelection(sceneContext, drag);
    if (drag.assetInstantiatesPrefab) {
        return sceneContext.CreatePrefabAssetEntity(
            drag.assetPath, drag.assetVirtualPath, position, false);
    }
    if (drag.assetCreatesMeshEntity) {
        return sceneContext.CreateMeshAssetEntity(drag.assetId, position, false);
    }
    if (drag.assetCreatesParticleEffectEntity) {
        return sceneContext.CreateParticleEffectEntity(drag.assetId, position, false);
    }
    return {};
}

[[nodiscard]] const char* CommandLabel(const EditorPointerDragState& drag) noexcept {
    if (drag.assetInstantiatesPrefab) {
        return "Instantiate Prefab";
    }
    if (drag.assetCreatesParticleEffectEntity) {
        return "Create Particle Effect Entity";
    }
    return "Create Mesh Entity";
}

[[nodiscard]] bool DestroyPreview(EditorSceneContext& sceneContext, EditorPointerDragState& drag) noexcept {
    if (!PreviewAlive(sceneContext, drag)) {
        drag.scenePlacementPreview = {};
        return false;
    }

    sceneContext.Scene().Entities().Destroy(drag.scenePlacementPreview);
    drag.scenePlacementPreview = {};
    RestorePreviousSelection(sceneContext, drag);
    sceneContext.MarkSceneRenderDirty();
    return true;
}

} // namespace

bool EditorSceneViewportAssetDragPreview::Update(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    if (!drag.CreatesScenePlacement()) {
        return false;
    }

    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveGroundHit(
            sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return DestroyPreview(sceneContext, drag);
    }

    const kb::scene::Vec3 position =
        sceneContext.ViewportPreview(hit->panelId).SnapGroundPosition(hit->groundPosition);
    if (!PreviewAlive(sceneContext, drag)) {
        drag.scenePlacementPreview = CreatePreview(sceneContext, drag, position);
        drag.scenePlacementPreviewCommitted = false;
        if (!PreviewAlive(sceneContext, drag)) {
            return false;
        }
    }

    EditorSceneViewportMath::MoveEntityTo(
        sceneContext.Scene(), drag.scenePlacementPreview, position);
    sceneContext.SelectEntity(drag.scenePlacementPreview);
    const std::array<kb::scene::SceneEntity, 1U> moved{ drag.scenePlacementPreview };
    sceneContext.MarkSceneEntitiesRenderDirty(moved);
    sceneContext.Scene().Runtime().SynchronizeTransforms();
    return true;
}

bool EditorSceneViewportAssetDragPreview::Commit(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    if (!drag.CreatesScenePlacement()) {
        return false;
    }

    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveGroundHit(
            sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    const kb::scene::Vec3 position =
        sceneContext.ViewportPreview(hit->panelId).SnapGroundPosition(hit->groundPosition);
    if (!PreviewAlive(sceneContext, drag)) {
        drag.scenePlacementPreview = CreatePreview(sceneContext, drag, position);
        if (!PreviewAlive(sceneContext, drag)) {
            return false;
        }
    }

    EditorSceneViewportMath::MoveEntityTo(
        sceneContext.Scene(), drag.scenePlacementPreview, position);
    sceneContext.SelectEntity(drag.scenePlacementPreview);
    const std::array<kb::scene::SceneEntity, 1U> created{ drag.scenePlacementPreview };
    sceneContext.MarkSceneEntitiesRenderDirty(created);
    drag.scenePlacementPreviewCommitted = sceneContext.AdoptCreatedHierarchyEntities(
        CommandLabel(drag), created);
    if (!drag.scenePlacementPreviewCommitted) {
        static_cast<void>(DestroyPreview(sceneContext, drag));
    }
    return drag.scenePlacementPreviewCommitted;
}

void EditorSceneViewportAssetDragPreview::Cancel(
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) noexcept {
    if (drag.scenePlacementPreviewCommitted) {
        return;
    }
    static_cast<void>(DestroyPreview(sceneContext, drag));
}

} // namespace kb::editor

#endif
