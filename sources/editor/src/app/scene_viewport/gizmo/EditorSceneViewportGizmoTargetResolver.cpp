#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoTargetResolver.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"

namespace kb::editor {

std::optional<kb::scene::Vec3> EditorSceneViewportGizmoTargetResolver::SelectedTarget(
    EditorSceneContext& sceneContext, const kb::math::DVec3& viewportOrigin) noexcept {
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        return std::nullopt;
    }

    const std::optional<kb::math::DVec3> pivot = EditorSceneSelectionPivot::ResolvePrecise(
        sceneContext.Scene(),
        sceneContext.SelectedHierarchyEntities(),
        selected);
    if (!pivot.has_value()) {
        return std::nullopt;
    }
    return kb::math::RelativeTo(*pivot, viewportOrigin);
}

} // namespace kb::editor
