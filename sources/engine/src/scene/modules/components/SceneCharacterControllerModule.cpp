#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneCharacterControllerComponentQueries::SceneCharacterControllerComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneCharacterControllerComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCharacterController(scene_, entity);
}

const CharacterControllerComponent* SceneCharacterControllerComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCharacterController(scene_, entity);
}

SceneCharacterControllerComponents::SceneCharacterControllerComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneCharacterControllerComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCharacterController(scene_, entity);
}

const CharacterControllerComponent* SceneCharacterControllerComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCharacterController(scene_, entity);
}

CharacterControllerComponent* SceneCharacterControllerComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetCharacterController(scene_, entity);
}

void SceneCharacterControllerComponents::Set(SceneEntity entity, const CharacterControllerComponent& characterController) {
    SceneComponentMutationService::SetCharacterController(scene_, entity, characterController);
}

void SceneCharacterControllerComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveCharacterController(scene_, entity);
}

void SceneCharacterControllerComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkCharacterControllerModified(scene_, entity);
}

} // namespace kb::scene
