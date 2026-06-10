#include "app/EditorBehaviourAssetSceneDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

bool EditorBehaviourAssetSceneDropHandler::Drop(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId) {
    const std::optional<RECT> scene = EditorDropPanelResolver::Resolve(DockPanelKind::Scene, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!scene.has_value() || !Contains(*scene, x, y)) {
        return false;
    }

    // Pick the entity under the cursor. SelectAt returns true when the ray hits
    // the ground plane and selects the entity it lands on (or clears selection on
    // empty ground), so a valid selection afterwards means an object was hit.
    if (!EditorSceneViewportObjectInteraction::SelectAt(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext)) {
        return false;
    }
    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (!entity.IsValid() || !sceneContext.Scene().Entities().IsAlive(entity)) {
        return false;
    }
    return sceneContext.AddBehaviourAssetToEntity(assetId, entity);
}

} // namespace kb::editor

#endif
