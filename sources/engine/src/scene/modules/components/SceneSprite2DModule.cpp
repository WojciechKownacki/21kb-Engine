#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneSprite2DComponentQueries::SceneSprite2DComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneSprite2DComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasSprite2D(scene_, entity);
}
const Sprite2DComponent* SceneSprite2DComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetSprite2D(scene_, entity);
}
SceneSprite2DComponents::SceneSprite2DComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneSprite2DComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasSprite2D(scene_, entity);
}
const Sprite2DComponent* SceneSprite2DComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetSprite2D(scene_, entity);
}
Sprite2DComponent* SceneSprite2DComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetSprite2D(scene_, entity);
}
void SceneSprite2DComponents::Set(SceneEntity entity, const Sprite2DComponent& component) {
    SceneComponentMutationService::SetSprite2D(scene_, entity, component);
}
void SceneSprite2DComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveSprite2D(scene_, entity);
}
void SceneSprite2DComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkSprite2DModified(scene_, entity);
}
} // namespace kb::scene
