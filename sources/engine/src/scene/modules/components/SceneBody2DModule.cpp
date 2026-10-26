#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneBody2DComponentQueries::SceneBody2DComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneBody2DComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasBody2D(scene_, entity);
}
const Body2DComponent* SceneBody2DComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetBody2D(scene_, entity);
}
SceneBody2DComponents::SceneBody2DComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneBody2DComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasBody2D(scene_, entity);
}
const Body2DComponent* SceneBody2DComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetBody2D(scene_, entity);
}
Body2DComponent* SceneBody2DComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetBody2D(scene_, entity);
}
void SceneBody2DComponents::Set(SceneEntity entity, const Body2DComponent& component) {
    SceneComponentMutationService::SetBody2D(scene_, entity, component);
}
void SceneBody2DComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveBody2D(scene_, entity);
}
void SceneBody2DComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkBody2DModified(scene_, entity);
}
} // namespace kb::scene
