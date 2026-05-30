#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneVisibilityComponentQueries::SceneVisibilityComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

VisibilityComponent SceneVisibilityComponentQueries::Get(SceneEntity entity) const {
    return SceneComponentQueryService::Visibility(scene_, entity);
}

const VisibilityComponent* SceneVisibilityComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetVisibility(scene_, entity);
}

SceneVisibilityComponents::SceneVisibilityComponents(Scene& scene) noexcept
    : scene_(scene) {}

VisibilityComponent SceneVisibilityComponents::Get(SceneEntity entity) const {
    return SceneComponentQueryService::Visibility(scene_, entity);
}

const VisibilityComponent* SceneVisibilityComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetVisibility(scene_, entity);
}

VisibilityComponent* SceneVisibilityComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetVisibility(scene_, entity);
}

void SceneVisibilityComponents::Set(SceneEntity entity, const VisibilityComponent& visibility) {
    SceneComponentMutationService::SetVisibility(scene_, entity, visibility);
}

void SceneVisibilityComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkVisibilityModified(scene_, entity);
}

} // namespace kb::scene
