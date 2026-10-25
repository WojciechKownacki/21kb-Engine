#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasTilemapCollider(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) &&
           SceneAccess::State(scene).componentStorage.TilemapColliders().Has(entity);
}
const TilemapColliderComponent* SceneComponentQueryService::TryGetTilemapCollider(const Scene& scene,
                                                                                  SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.TilemapColliders().TryGet(entity)
               : nullptr;
}
TilemapColliderComponent* SceneComponentMutationService::TryGetTilemapCollider(Scene& scene,
                                                                               SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.TilemapColliders().TryGet(entity)
               : nullptr;
}
void SceneComponentMutationService::SetTilemapCollider(Scene& scene, SceneEntity entity,
                                                       const TilemapColliderComponent& component) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.TilemapColliders().Set(entity, component);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::RemoveTilemapCollider(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.TilemapColliders().Remove(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::MarkTilemapColliderModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.TilemapColliders().MarkModified(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
