#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoTargetResolver.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"

namespace kb::editor {

std::optional<kb::scene::Vec3> EditorSceneViewportGizmoTargetResolver::SelectedTarget(EditorSceneContext& sceneContext) noexcept {
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        return std::nullopt;
    }

    return EditorSceneSelectionPivot::Resolve(
        sceneContext.Scene(),
        sceneContext.SelectedHierarchyEntities(),
        selected);
}

} // namespace kb::editor
