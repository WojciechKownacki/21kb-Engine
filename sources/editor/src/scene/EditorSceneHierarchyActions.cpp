#include "scene/EditorSceneHierarchyActions.hpp"

#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"
#include "scene/EditorHierarchyObjectFactory.hpp"

namespace kb::editor {

bool EditorSceneHierarchyActions::ToggleVisibility(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }

    kb::scene::VisibilityComponent visibility = scene.Components().Visibility().Get(entity);
    visibility.mode = visibility.mode == kb::scene::VisibilityMode::Hidden
        ? kb::scene::VisibilityMode::Visible
        : kb::scene::VisibilityMode::Hidden;
    scene.Components().Visibility().Set(entity, visibility);
    return true;
}

kb::scene::SceneEntity EditorSceneHierarchyActions::CreateObject(kb::scene::Scene& scene) {
    return EditorHierarchyObjectFactory::CreateObject(scene);
}

bool EditorSceneHierarchyActions::Reparent(kb::scene::Scene& scene, kb::scene::SceneEntity child, kb::scene::SceneEntity parent) {
    if (!child.IsValid() || child == parent || !scene.Entities().IsAlive(child)) {
        return false;
    }
    return scene.Hierarchy().SetParent(child, parent);
}

} // namespace kb::editor
