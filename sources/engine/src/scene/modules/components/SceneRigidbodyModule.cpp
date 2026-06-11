#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneRigidbodyComponentQueries::SceneRigidbodyComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneRigidbodyComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasRigidbody(scene_, entity);
}

const RigidbodyComponent* SceneRigidbodyComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetRigidbody(scene_, entity);
}

SceneRigidbodyComponents::SceneRigidbodyComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneRigidbodyComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasRigidbody(scene_, entity);
}

const RigidbodyComponent* SceneRigidbodyComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetRigidbody(scene_, entity);
}

RigidbodyComponent* SceneRigidbodyComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetRigidbody(scene_, entity);
}

void SceneRigidbodyComponents::Set(SceneEntity entity, const RigidbodyComponent& rigidbody) {
    SceneComponentMutationService::SetRigidbody(scene_, entity, rigidbody);
}

void SceneRigidbodyComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveRigidbody(scene_, entity);
}

void SceneRigidbodyComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkRigidbodyModified(scene_, entity);
}

} // namespace kb::scene
