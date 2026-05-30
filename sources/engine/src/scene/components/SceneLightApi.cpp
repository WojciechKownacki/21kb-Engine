#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasLight(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Lights().Has(entity);
}

const LightComponent* SceneComponentQueryService::TryGetLight(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Lights().TryGet(entity) : nullptr;
}

LightComponent* SceneComponentMutationService::TryGetLight(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Lights().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetLight(Scene& scene, SceneEntity entity, const LightComponent& light) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Lights().Set(entity, light);
    }
}

void SceneComponentMutationService::RemoveLight(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Lights().Remove(entity);
    }
}

void SceneComponentMutationService::MarkLightModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Lights().MarkModified(entity);
    }
}

} // namespace kb::scene
