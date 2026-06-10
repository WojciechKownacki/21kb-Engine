#include "app/EditorBehaviourAssetInspectorDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

bool EditorBehaviourAssetInspectorDropHandler::Drop(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId) {
    const std::optional<RECT> inspector = EditorDropPanelResolver::Resolve(DockPanelKind::Inspector, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!inspector.has_value() || !Contains(*inspector, x, y)) {
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
