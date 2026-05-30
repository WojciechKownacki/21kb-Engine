#include "scene/components/SceneComponentStorage.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

const TransformComponent* SceneComponentStorage::TryGetTransform(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<TransformComponent>(world_, entity, components_.TransformComponentId());
}

TransformComponent* SceneComponentStorage::TryGetTransform(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<TransformComponent>(world_, entity, components_.TransformComponentId());
}

void SceneComponentStorage::SetTransform(SceneEntity entity, const TransformComponent& transform) {
    SceneComponentStorageAccess::Set(world_, entity, components_.TransformComponentId(), transform);
}

void SceneComponentStorage::MarkTransformModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, components_.TransformComponentId());
}

} // namespace kb::scene
