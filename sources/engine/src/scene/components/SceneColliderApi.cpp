#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasCollider(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Colliders().Has(entity);
}

const ColliderComponent* SceneComponentQueryService::TryGetCollider(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Colliders().TryGet(entity) : nullptr;
}

ColliderComponent* SceneComponentMutationService::TryGetCollider(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Colliders().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetCollider(Scene& scene, SceneEntity entity, const ColliderComponent& collider) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Colliders().Set(entity, collider);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::RemoveCollider(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Colliders().Remove(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkColliderModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Colliders().MarkModified(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
