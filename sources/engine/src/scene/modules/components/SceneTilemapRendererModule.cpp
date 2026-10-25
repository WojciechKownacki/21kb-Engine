#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneTilemapRendererComponentQueries::SceneTilemapRendererComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}
bool SceneTilemapRendererComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasTilemapRenderer(scene_, entity);
}
const TilemapRendererComponent* SceneTilemapRendererComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetTilemapRenderer(scene_, entity);
}
SceneTilemapRendererComponents::SceneTilemapRendererComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneTilemapRendererComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasTilemapRenderer(scene_, entity);
}
const TilemapRendererComponent* SceneTilemapRendererComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetTilemapRenderer(scene_, entity);
}
TilemapRendererComponent* SceneTilemapRendererComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetTilemapRenderer(scene_, entity);
}
void SceneTilemapRendererComponents::Set(SceneEntity entity, const TilemapRendererComponent& component) {
    SceneComponentMutationService::SetTilemapRenderer(scene_, entity, component);
}
void SceneTilemapRendererComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveTilemapRenderer(scene_, entity);
}
void SceneTilemapRendererComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkTilemapRendererModified(scene_, entity);
}
} // namespace kb::scene
