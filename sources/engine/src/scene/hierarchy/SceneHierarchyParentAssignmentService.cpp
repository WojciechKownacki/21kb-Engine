#include "scene/hierarchy/SceneHierarchyParentAssignmentService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/hierarchy/SceneHierarchyOperations.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

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
    if (!state.world.IsAlive(child) || (parent.IsValid() && !state.world.IsAlive(parent)) || child == parent) {
        return false;
    }
    const SceneEntity oldParent = SceneHierarchyCache::Parent(state, child);
    if (oldParent == parent) {
        return false;
    }
    // Validate the scene's own hierarchy even when entities are not mirrored to the backend.
    // A second cursor detects an already corrupt cycle without allocating traversal storage.
    SceneEntity ancestor = parent;
    SceneEntity fastAncestor = parent;
    while (ancestor.IsValid()) {
        if (ancestor == child) {
            return false;
        }
        ancestor = SceneHierarchyCache::Parent(state, ancestor);
        if (fastAncestor.IsValid()) fastAncestor = SceneHierarchyCache::Parent(state, fastAncestor);
        if (fastAncestor.IsValid()) fastAncestor = SceneHierarchyCache::Parent(state, fastAncestor);
        if (ancestor.IsValid() && ancestor == fastAncestor) return false;
    }

    SceneHistoryService::NoteObjectMoving(scene, child, parent);
    const bool backendChildAlive = ecs_is_alive(state.world.NativeHandle(), kb::ecs::FlecsEntityId(child));
    const bool backendParentAlive = !parent.IsValid() || ecs_is_alive(state.world.NativeHandle(), kb::ecs::FlecsEntityId(parent));
    if (!backendChildAlive || !backendParentAlive) {
        SceneHierarchyCache::Move(state, child, oldParent, parent);
        SceneTransformService::MarkParentModified(scene, child);
        MarkSceneRenderProxySubtreeDirty(state, child);
        MarkScenePrefabTopologyDirty(state, child);
        MarkScenePrefabTopologyDirty(state, oldParent);
        MarkScenePrefabTopologyDirty(state, parent);
        return true;
    }

    const bool changed = SceneHierarchyOperations::SetParent(state.world, child, parent);
    if (changed) {
        SceneHierarchyCache::Move(state, child, oldParent, parent);
        SceneTransformService::MarkParentModified(scene, child);
        MarkSceneRenderProxySubtreeDirty(state, child);
        MarkScenePrefabTopologyDirty(state, child);
        MarkScenePrefabTopologyDirty(state, oldParent);
        MarkScenePrefabTopologyDirty(state, parent);
    }
    return changed;
}

} // namespace kb::scene
