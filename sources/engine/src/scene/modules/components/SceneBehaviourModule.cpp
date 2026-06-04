#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneIterationService.hpp"

namespace kb::scene {

SceneBehaviourComponentQueries::SceneBehaviourComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneBehaviourComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasBehaviour(scene_, entity);
}

const BehaviourComponent* SceneBehaviourComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetBehaviour(scene_, entity);
}

void SceneBehaviourComponentQueries::ForEach(BehaviourVisitor visitor, void* context) const {
    SceneIterationService::ForEachBehaviour(scene_, visitor, context);
}

SceneBehaviourComponents::SceneBehaviourComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneBehaviourComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasBehaviour(scene_, entity);
}

const BehaviourComponent* SceneBehaviourComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetBehaviour(scene_, entity);
}

BehaviourComponent* SceneBehaviourComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetBehaviour(scene_, entity);
}

void SceneBehaviourComponents::Set(SceneEntity entity, const BehaviourComponent& behaviour) {
    SceneComponentMutationService::SetBehaviour(scene_, entity, behaviour);
}

void SceneBehaviourComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveBehaviour(scene_, entity);
}

void SceneBehaviourComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkBehaviourModified(scene_, entity);
}

void SceneBehaviourComponents::ForEach(BehaviourVisitor visitor, void* context) const {
    SceneIterationService::ForEachBehaviour(scene_, visitor, context);
}

} // namespace kb::scene
