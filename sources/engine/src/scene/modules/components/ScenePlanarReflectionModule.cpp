#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
ScenePlanarReflectionComponentQueries::ScenePlanarReflectionComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool ScenePlanarReflectionComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasPlanarReflection(scene_, entity);
}
const PlanarReflectionComponent* ScenePlanarReflectionComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetPlanarReflection(scene_, entity);
}
ScenePlanarReflectionComponents::ScenePlanarReflectionComponents(Scene& scene) noexcept : scene_(scene) {}
bool ScenePlanarReflectionComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasPlanarReflection(scene_, entity);
}
const PlanarReflectionComponent* ScenePlanarReflectionComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetPlanarReflection(scene_, entity);
}
PlanarReflectionComponent* ScenePlanarReflectionComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetPlanarReflection(scene_, entity);
}
void ScenePlanarReflectionComponents::Set(SceneEntity entity, const PlanarReflectionComponent& component) {
    SceneComponentMutationService::SetPlanarReflection(scene_, entity, component);
}
void ScenePlanarReflectionComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemovePlanarReflection(scene_, entity);
}
void ScenePlanarReflectionComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkPlanarReflectionModified(scene_, entity);
}
} // namespace kb::scene
