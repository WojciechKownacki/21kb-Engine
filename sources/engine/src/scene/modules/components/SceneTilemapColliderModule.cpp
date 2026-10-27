#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneTilemapColliderComponentQueries::SceneTilemapColliderComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}
bool SceneTilemapColliderComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasTilemapCollider(scene_, entity);
}
const TilemapColliderComponent* SceneTilemapColliderComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetTilemapCollider(scene_, entity);
}
SceneTilemapColliderComponents::SceneTilemapColliderComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneTilemapColliderComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasTilemapCollider(scene_, entity);
}
const TilemapColliderComponent* SceneTilemapColliderComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetTilemapCollider(scene_, entity);
}
TilemapColliderComponent* SceneTilemapColliderComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetTilemapCollider(scene_, entity);
}
void SceneTilemapColliderComponents::Set(SceneEntity entity, const TilemapColliderComponent& component) {
    SceneComponentMutationService::SetTilemapCollider(scene_, entity, component);
}
void SceneTilemapColliderComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveTilemapCollider(scene_, entity);
}
void SceneTilemapColliderComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkTilemapColliderModified(scene_, entity);
}
} // namespace kb::scene
