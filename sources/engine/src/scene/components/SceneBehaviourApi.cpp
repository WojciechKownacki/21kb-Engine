#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasBehaviour(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Behaviours().Has(entity);
}

const BehaviourComponent* SceneComponentQueryService::TryGetBehaviour(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Behaviours().TryGet(entity) : nullptr;
}

BehaviourComponent* SceneComponentMutationService::TryGetBehaviour(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Behaviours().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetBehaviour(Scene& scene, SceneEntity entity, const BehaviourComponent& behaviour) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Behaviours().Set(entity, behaviour);
    }
}

void SceneComponentMutationService::RemoveBehaviour(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Behaviours().Remove(entity);
    }
}

void SceneComponentMutationService::MarkBehaviourModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneAccess::State(scene).componentStorage.Behaviours().MarkModified(entity);
    }
}

} // namespace kb::scene
