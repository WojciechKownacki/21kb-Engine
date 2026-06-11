#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

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
        SceneAccess::State(scene).componentStorage.Colliders().Set(entity, collider);
    }
}

void SceneComponentMutationService::RemoveCollider(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Colliders().Remove(entity);
    }
}

void SceneComponentMutationService::MarkColliderModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Colliders().MarkModified(entity);
    }
}

} // namespace kb::scene
