#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneTilemapComponentQueries::SceneTilemapComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneTilemapComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasTilemap(scene_, entity);
}
const TilemapComponent* SceneTilemapComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetTilemap(scene_, entity);
}
SceneTilemapComponents::SceneTilemapComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneTilemapComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasTilemap(scene_, entity);
}
const TilemapComponent* SceneTilemapComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetTilemap(scene_, entity);
}
TilemapComponent* SceneTilemapComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetTilemap(scene_, entity);
}
void SceneTilemapComponents::Set(SceneEntity entity, const TilemapComponent& component) {
    SceneComponentMutationService::SetTilemap(scene_, entity, component);
}
void SceneTilemapComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveTilemap(scene_, entity);
}
void SceneTilemapComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkTilemapModified(scene_, entity);
}
} // namespace kb::scene
