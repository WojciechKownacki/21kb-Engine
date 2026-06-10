#include "engine/scene/SceneInputComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneInputComponentQueries::SceneInputComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneInputComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasInput(scene_, entity);
}

const InputComponent* SceneInputComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetInput(scene_, entity);
}

SceneInputComponents::SceneInputComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneInputComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasInput(scene_, entity);
}

const InputComponent* SceneInputComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetInput(scene_, entity);
}

InputComponent* SceneInputComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetInput(scene_, entity);
}

void SceneInputComponents::Set(SceneEntity entity, const InputComponent& input) {
    SceneComponentMutationService::SetInput(scene_, entity, input);
}

void SceneInputComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveInput(scene_, entity);
}

void SceneInputComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkInputModified(scene_, entity);
}

} // namespace kb::scene
