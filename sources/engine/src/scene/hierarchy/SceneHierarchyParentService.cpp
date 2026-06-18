#include "scene/hierarchy/SceneHierarchyParentService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

namespace kb::scene {

SceneObject SceneHierarchyParentService::Parent(Scene& scene, SceneObject object) {
    if (!SceneEntityService::IsAlive(scene, object)) {
        return {};
    }

    const SceneEntity parent = Parent(scene, object.Entity());
    return parent.IsValid() ? SceneAccess::MakeObject(scene, parent) : SceneObject{};
}

SceneEntity SceneHierarchyParentService::Parent(const Scene& scene, SceneEntity entity) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    if (!state.world.IsAlive(entity)) {
        return {};
    }
    return SceneHierarchyCache::Parent(state, entity);
}

} // namespace kb::scene
