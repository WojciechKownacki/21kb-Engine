#include "app/scene_viewport/EditorSceneViewportMeshDragPreview.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <array>
#include <optional>
#include <span>

namespace kb::editor {

bool EditorSceneViewportMeshDragPreview::Update(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    if (!drag.assetCreatesMeshEntity || !drag.assetId.IsValid()) {
        return false;
    }

    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveGroundHit(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    if (!drag.meshScenePreview.IsValid() || !sceneContext.Scene().Entities().IsAlive(drag.meshScenePreview)) {
        const kb::scene::Vec3 snappedPosition = sceneContext.ViewportPreview(hit->panelId).SnapGroundPosition(hit->groundPosition);
        drag.meshScenePreview = sceneContext.CreateMeshAssetEntity(drag.assetId, snappedPosition, false);
        drag.meshScenePreviewCommitted = false;
        if (!drag.meshScenePreview.IsValid()) {
            return false;
        }
        const std::array<kb::scene::SceneEntity, 1U> created{ drag.meshScenePreview };
        sceneContext.MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity>{ created.data(), created.size() });
    }

    EditorSceneViewportMath::MoveEntityTo(
        sceneContext.Scene(),
        drag.meshScenePreview,
        sceneContext.ViewportPreview(hit->panelId).SnapGroundPosition(hit->groundPosition));
    sceneContext.SelectEntity(drag.meshScenePreview);
    const std::array<kb::scene::SceneEntity, 1U> moved{ drag.meshScenePreview };
    sceneContext.MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity>{ moved.data(), moved.size() });
    return true;
}

bool EditorSceneViewportMeshDragPreview::Commit(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorPointerDragState& drag) {
    if (!drag.assetCreatesMeshEntity) {
        return false;
    }

    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveGroundHit(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    if (drag.meshScenePreview.IsValid() && sceneContext.Scene().Entities().IsAlive(drag.meshScenePreview)) {
        EditorSceneViewportMath::MoveEntityTo(
            sceneContext.Scene(),
            drag.meshScenePreview,
            sceneContext.ViewportPreview(hit->panelId).SnapGroundPosition(hit->groundPosition));
        sceneContext.SelectEntity(drag.meshScenePreview);
        const std::array<kb::scene::SceneEntity, 1U> created{ drag.meshScenePreview };
        sceneContext.MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity>{ created.data(), created.size() });
        drag.meshScenePreviewCommitted = sceneContext.AdoptCreatedHierarchyEntities(
            "Create Mesh Entity",
            std::span<const kb::scene::SceneEntity>{ created.data(), created.size() });
        if (!drag.meshScenePreviewCommitted) {
            sceneContext.Scene().Entities().Destroy(drag.meshScenePreview);
            drag.meshScenePreview = {};
            sceneContext.MarkSceneRenderDirty();
        }
        return drag.meshScenePreviewCommitted;
    }

    drag.meshScenePreview = sceneContext.CreateMeshAssetEntity(
        drag.assetId,
        sceneContext.ViewportPreview(hit->panelId).SnapGroundPosition(hit->groundPosition),
        false);
    const std::array<kb::scene::SceneEntity, 1U> created{ drag.meshScenePreview };
    drag.meshScenePreviewCommitted = drag.meshScenePreview.IsValid() && sceneContext.AdoptCreatedHierarchyEntities(
        "Create Mesh Entity",
        std::span<const kb::scene::SceneEntity>{ created.data(), created.size() });
    if (!drag.meshScenePreviewCommitted && drag.meshScenePreview.IsValid() && sceneContext.Scene().Entities().IsAlive(drag.meshScenePreview)) {
        sceneContext.Scene().Entities().Destroy(drag.meshScenePreview);
        drag.meshScenePreview = {};
        sceneContext.MarkSceneRenderDirty();
    }
    return drag.meshScenePreviewCommitted;
}

void EditorSceneViewportMeshDragPreview::Cancel(EditorSceneContext& sceneContext, EditorPointerDragState& drag) noexcept {
    if (drag.meshScenePreviewCommitted || !drag.meshScenePreview.IsValid() || !sceneContext.Scene().Entities().IsAlive(drag.meshScenePreview)) {
        return;
    }

    sceneContext.Scene().Entities().Destroy(drag.meshScenePreview);
    if (sceneContext.SelectedEntity() == drag.meshScenePreview) {
        sceneContext.ClearHierarchySelection();
    }
    sceneContext.MarkSceneRenderDirty();
    drag.meshScenePreview = {};
}

} // namespace kb::editor

#endif
