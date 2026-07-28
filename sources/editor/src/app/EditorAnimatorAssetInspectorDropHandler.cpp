#include "app/EditorAnimatorAssetInspectorDropHandler.hpp"

#if defined(_WIN32)
#include "app/EditorDropPanelResolver.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "rendering/InspectorPanelRenderer.hpp"

#include <optional>

namespace kb::editor {

bool EditorAnimatorAssetInspectorDropHandler::Drop(
    HWND sourceWindow, HWND mainWindow, int x, int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId) {
    const std::optional<RECT> inspector = EditorDropPanelResolver::Resolve(
        DockPanelKind::Inspector, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!inspector.has_value() || x < inspector->left || x >= inspector->right ||
        y < inspector->top || y >= inspector->bottom) return false;
    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (!entity.IsValid() || !sceneContext.Scene().Entities().IsAlive(entity)) return false;
    const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(*inspector, sceneContext, x, y);
    if (hit.section != InspectorSectionId::Animator ||
        hit.property != InspectorPropertyId::AnimatorController) return false;
    return sceneContext.SetAnimatorControllerAsset(entity, assetId);
}

} // namespace kb::editor
#endif
