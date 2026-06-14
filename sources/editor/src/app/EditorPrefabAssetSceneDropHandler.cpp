#include "app/EditorPrefabAssetSceneDropHandler.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"

#include <optional>

namespace kb::editor {

bool EditorPrefabAssetSceneDropHandler::Drop(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    const std::filesystem::path& assetPath,
    const std::filesystem::path& assetVirtualPath) {
    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveGroundHit(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    return sceneContext.InstantiatePrefabAssetAt(
        assetPath,
        assetVirtualPath,
        sceneContext.ViewportPreview(hit->panelId).SnapGroundPosition(hit->groundPosition));
}

} // namespace kb::editor

#endif
