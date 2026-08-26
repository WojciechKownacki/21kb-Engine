#include "app/EditorMeshAssetSceneDropHandler.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"

#include <optional>

namespace kb::editor {
bool EditorMeshAssetSceneDropHandler::Drop(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId) {
    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveGroundHit(
            sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    return sceneContext.CreateMeshAssetEntity(
        assetId,
        sceneContext.ViewportPreview(hit->panelId).SnapGroundPosition(hit->groundPosition),
        true).IsValid();
}

} // namespace kb::editor

#endif
