#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneCollider2DComponentQueries::SceneCollider2DComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneCollider2DComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCollider2D(scene_, entity);
}
const Collider2DComponent* SceneCollider2DComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCollider2D(scene_, entity);
}
SceneCollider2DComponents::SceneCollider2DComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneCollider2DComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCollider2D(scene_, entity);
}
const Collider2DComponent* SceneCollider2DComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCollider2D(scene_, entity);
}
Collider2DComponent* SceneCollider2DComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetCollider2D(scene_, entity);
}
void SceneCollider2DComponents::Set(SceneEntity entity, const Collider2DComponent& component) {
    SceneComponentMutationService::SetCollider2D(scene_, entity, component);
}
void SceneCollider2DComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveCollider2D(scene_, entity);
}
void SceneCollider2DComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkCollider2DModified(scene_, entity);
}
} // namespace kb::scene
