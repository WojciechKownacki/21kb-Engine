#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasInput(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Inputs().Has(entity);
}

const InputComponent* SceneComponentQueryService::TryGetInput(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Inputs().TryGet(entity) : nullptr;
}

InputComponent* SceneComponentMutationService::TryGetInput(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Inputs().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetInput(Scene& scene, SceneEntity entity, const InputComponent& input) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Inputs().Set(entity, input);
    }
}

void SceneComponentMutationService::RemoveInput(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Inputs().Remove(entity);
    }
}

void SceneComponentMutationService::MarkInputModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Inputs().MarkModified(entity);
    }
}

} // namespace kb::scene
