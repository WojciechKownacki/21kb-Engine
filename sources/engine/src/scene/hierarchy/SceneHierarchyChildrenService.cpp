#include "scene/hierarchy/SceneHierarchyChildrenService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyOperations.hpp"

namespace kb::scene {

std::vector<SceneObject> SceneHierarchyChildrenService::Children(Scene& scene, SceneObject object) {
    if (!SceneEntityService::IsAlive(scene, object)) {
        return {};
    }

    std::vector<SceneObject> objects;
    for (const SceneEntity child : ChildEntities(scene, object.Entity())) {
        objects.push_back(SceneAccess::MakeObject(scene, child));
    }

    return objects;
}

std::vector<SceneEntity> SceneHierarchyChildrenService::ChildEntities(const Scene& scene, SceneEntity entity) {
    return SceneHierarchyOperations::Children(SceneAccess::State(scene).world, entity);
}

} // namespace kb::scene
