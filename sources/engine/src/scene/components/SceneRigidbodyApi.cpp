#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasRigidbody(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Rigidbodies().Has(entity);
}

const RigidbodyComponent* SceneComponentQueryService::TryGetRigidbody(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Rigidbodies().TryGet(entity) : nullptr;
}

RigidbodyComponent* SceneComponentMutationService::TryGetRigidbody(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Rigidbodies().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetRigidbody(Scene& scene, SceneEntity entity, const RigidbodyComponent& rigidbody) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Rigidbodies().Set(entity, rigidbody);
    }
}

void SceneComponentMutationService::RemoveRigidbody(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Rigidbodies().Remove(entity);
    }
}

void SceneComponentMutationService::MarkRigidbodyModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Rigidbodies().MarkModified(entity);
    }
}

} // namespace kb::scene
