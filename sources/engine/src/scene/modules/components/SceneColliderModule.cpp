#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneColliderComponentQueries::SceneColliderComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneColliderComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCollider(scene_, entity);
}

const ColliderComponent* SceneColliderComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCollider(scene_, entity);
}

SceneColliderComponents::SceneColliderComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneColliderComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCollider(scene_, entity);
}

const ColliderComponent* SceneColliderComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCollider(scene_, entity);
}

ColliderComponent* SceneColliderComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetCollider(scene_, entity);
}

void SceneColliderComponents::Set(SceneEntity entity, const ColliderComponent& collider) {
    SceneComponentMutationService::SetCollider(scene_, entity, collider);
}

void SceneColliderComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveCollider(scene_, entity);
}

void SceneColliderComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkColliderModified(scene_, entity);
}

} // namespace kb::scene
