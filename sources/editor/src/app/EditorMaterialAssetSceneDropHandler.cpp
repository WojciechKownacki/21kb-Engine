#include "app/EditorMaterialAssetSceneDropHandler.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "app/scene_viewport/EditorSceneViewportMeshPicker.hpp"

#include <optional>

namespace kb::editor {

bool EditorMaterialAssetSceneDropHandler::Drop(
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
        EditorSceneViewportHitResolver::ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    const EditorSceneViewportPickResult pick = EditorSceneViewportMeshPicker::PickNearest(sceneContext.Scene(), hit->ray);
    if (!pick.IsValid()) {
        return false;
    }

    if (!sceneContext.SetMeshRendererMaterialAsset(pick.entity, assetId)) {
        return false;
    }
    sceneContext.SelectEntity(pick.entity);
    return true;
}

} // namespace kb::editor

#endif
