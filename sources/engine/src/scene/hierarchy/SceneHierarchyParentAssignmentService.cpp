#include "scene/hierarchy/SceneHierarchyParentAssignmentService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/hierarchy/SceneHierarchyOperations.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

namespace kb::scene {

bool SceneHierarchyParentAssignmentService::SetParent(Scene& scene, SceneObject child, SceneObject parent) noexcept {
    if (!SceneAccess::BelongsTo(scene, child)) {
        return false;
    }
    if (parent.Entity().IsValid() && !SceneAccess::BelongsTo(scene, parent)) {
        return false;
    }

    return SetParent(scene, child.Entity(), parent.Entity());
}

bool SceneHierarchyParentAssignmentService::SetParent(Scene& scene, SceneEntity child, SceneEntity parent) noexcept {
    SceneState& state = SceneAccess::State(scene);
    const SceneEntity oldParent = SceneHierarchyCache::Parent(state, child);
    const bool backendChildAlive = ecs_is_alive(state.world.NativeHandle(), kb::ecs::FlecsEntityId(child));
    const bool backendParentAlive = !parent.IsValid() || ecs_is_alive(state.world.NativeHandle(), kb::ecs::FlecsEntityId(parent));
    if (!backendChildAlive || !backendParentAlive) {
        if (!state.world.IsAlive(child) || (parent.IsValid() && !state.world.IsAlive(parent)) || child == parent || oldParent == parent) {
            return false;
        }
        SceneHierarchyCache::Move(state, child, oldParent, parent);
        return true;
    }

    const bool changed = SceneHierarchyOperations::SetParent(state.world, child, parent);
    if (changed) {
        SceneHierarchyCache::Move(state, child, oldParent, parent);
        SceneTransformService::MarkParentModified(scene, child);
    }
    return changed;
}

} // namespace kb::scene
