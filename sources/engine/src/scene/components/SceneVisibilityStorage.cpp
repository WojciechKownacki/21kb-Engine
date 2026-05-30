#include "scene/components/SceneComponentStorage.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

const VisibilityComponent* SceneComponentStorage::TryGetVisibility(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<VisibilityComponent>(world_, entity, components_.VisibilityComponentId());
}

VisibilityComponent* SceneComponentStorage::TryGetVisibility(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<VisibilityComponent>(world_, entity, components_.VisibilityComponentId());
}

void SceneComponentStorage::SetVisibility(SceneEntity entity, const VisibilityComponent& visibility) {
    SceneComponentStorageAccess::Set(world_, entity, components_.VisibilityComponentId(), visibility);
}

void SceneComponentStorage::MarkVisibilityModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, components_.VisibilityComponentId());
}

} // namespace kb::scene
