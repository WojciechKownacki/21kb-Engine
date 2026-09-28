#include "scene/entities/SceneEntityDestructionService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHierarchyService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityNaming.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

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

    SceneState& state = SceneAccess::State(scene);
    const SceneEntity root = entity;
    for (;;) {
        const SceneObject object = SceneAccess::MakeObject(scene, entity);
        const ScenePrefabInstanceHandle rootInstance = state.prefabInstances.FindRootInstance(object);
        if (rootInstance.IsValid()) {
            static_cast<void>(state.prefabInstances.Remove(rootInstance));
        }
        MarkScenePrefabTopologyDirty(state, entity);
        MarkScenePrefabTopologyDirty(state, SceneHierarchyService::Parent(scene, entity));

        if (SceneHierarchyCache::ChildCount(state, entity) != 0U) {
            entity = SceneHierarchyCache::ChildAt(state, entity, 0U);
            continue;
        }

        // Parent links retain the traversal path while leaves are removed.
        do {
            const SceneEntity parent = SceneHierarchyService::Parent(scene, entity);
            SceneHierarchyCache::Remove(state, entity, parent);
            SceneEntityNaming::ClearName(state, entity);
            ClearSceneRenderProxyComponentMask(state, entity);
            state.world.DestroyEntity(entity);
            if (entity == root) {
                return;
            }
            entity = parent;
        } while (SceneHierarchyCache::ChildCount(state, entity) == 0U);
        entity = SceneHierarchyCache::ChildAt(state, entity, 0U);
    }
}

} // namespace kb::scene
