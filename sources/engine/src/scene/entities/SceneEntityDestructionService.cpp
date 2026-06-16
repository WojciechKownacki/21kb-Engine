#include "scene/entities/SceneEntityDestructionService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHierarchyService.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

namespace kb::scene {

void SceneEntityDestructionService::DestroyObject(Scene& scene, SceneObject object) noexcept {
    if (SceneEntityService::IsAlive(scene, object)) {
        DestroyEntity(scene, object.Entity());
    }
}

void SceneEntityDestructionService::DestroyEntity(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) {
        return;
    }

    for (const SceneEntity child : SceneHierarchyService::ChildEntities(scene, entity)) {
        DestroyEntity(scene, child);
    }

    SceneState& state = SceneAccess::State(scene);
    const SceneEntity parent = SceneHierarchyService::Parent(scene, entity);
    SceneHierarchyCache::Remove(state, entity, parent);
    state.world.DestroyEntity(entity);
}

} // namespace kb::scene
