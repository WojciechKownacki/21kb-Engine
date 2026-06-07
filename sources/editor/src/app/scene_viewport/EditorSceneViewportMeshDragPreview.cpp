#include "app/scene_viewport/EditorSceneViewportMeshDragPreview.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <optional>

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
        drag.meshScenePreview = sceneContext.CreateMeshAssetEntity(drag.assetId, hit->groundPosition, false);
        drag.meshScenePreviewCommitted = false;
        if (!drag.meshScenePreview.IsValid()) {
            return false;
        }
    }

    EditorSceneViewportMath::MoveEntityTo(sceneContext.Scene(), drag.meshScenePreview, hit->groundPosition);
    sceneContext.SelectEntity(drag.meshScenePreview);
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
        EditorSceneViewportMath::MoveEntityTo(sceneContext.Scene(), drag.meshScenePreview, hit->groundPosition);
        sceneContext.SelectEntity(drag.meshScenePreview);
        drag.meshScenePreviewCommitted = true;
        return true;
    }

    drag.meshScenePreview = sceneContext.CreateMeshAssetEntity(drag.assetId, hit->groundPosition, true);
    drag.meshScenePreviewCommitted = drag.meshScenePreview.IsValid();
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
    drag.meshScenePreview = {};
}

} // namespace kb::editor

#endif
