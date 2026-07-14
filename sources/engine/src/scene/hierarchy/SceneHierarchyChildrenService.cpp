#include "scene/hierarchy/SceneHierarchyChildrenService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

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
    const SceneState& state = SceneAccess::State(scene);
    return SceneHierarchyCache::Children(state, entity);
}

std::size_t SceneHierarchyChildrenService::ChildCount(const Scene& scene, SceneEntity entity) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return SceneHierarchyCache::ChildCount(state, entity);
}

SceneEntity SceneHierarchyChildrenService::ChildAt(const Scene& scene, SceneEntity entity, std::size_t index) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return SceneHierarchyCache::ChildAt(state, entity, index);
}

} // namespace kb::scene
