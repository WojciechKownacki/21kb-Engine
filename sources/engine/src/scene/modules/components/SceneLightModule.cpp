#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneLightComponentQueries::SceneLightComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneLightComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasLight(scene_, entity);
}

const LightComponent* SceneLightComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetLight(scene_, entity);
}

SceneLightComponents::SceneLightComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneLightComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasLight(scene_, entity);
}

const LightComponent* SceneLightComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetLight(scene_, entity);
}

LightComponent* SceneLightComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetLight(scene_, entity);
}

void SceneLightComponents::Set(SceneEntity entity, const LightComponent& light) {
    SceneComponentMutationService::SetLight(scene_, entity, light);
}

void SceneLightComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveLight(scene_, entity);
}

void SceneLightComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkLightModified(scene_, entity);
}

} // namespace kb::scene
