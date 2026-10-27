#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneCharacterController2DComponentQueries::SceneCharacterController2DComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneCharacterController2DComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCharacterController2D(scene_, entity);
}
const CharacterController2DComponent* SceneCharacterController2DComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCharacterController2D(scene_, entity);
}
SceneCharacterController2DComponents::SceneCharacterController2DComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneCharacterController2DComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasCharacterController2D(scene_, entity);
}
const CharacterController2DComponent* SceneCharacterController2DComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetCharacterController2D(scene_, entity);
}
CharacterController2DComponent* SceneCharacterController2DComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetCharacterController2D(scene_, entity);
}
void SceneCharacterController2DComponents::Set(SceneEntity entity, const CharacterController2DComponent& component) {
    SceneComponentMutationService::SetCharacterController2D(scene_, entity, component);
}
void SceneCharacterController2DComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveCharacterController2D(scene_, entity);
}
void SceneCharacterController2DComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkCharacterController2DModified(scene_, entity);
}
} // namespace kb::scene
