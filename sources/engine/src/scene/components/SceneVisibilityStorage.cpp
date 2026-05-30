#include "scene/components/SceneVisibilityComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneVisibilityComponentStore::SceneVisibilityComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

const VisibilityComponent* SceneVisibilityComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<VisibilityComponent>(world_, entity, componentId_);
}

VisibilityComponent* SceneVisibilityComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<VisibilityComponent>(world_, entity, componentId_);
}

void SceneVisibilityComponentStore::Set(SceneEntity entity, const VisibilityComponent& visibility) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, visibility);
}

void SceneVisibilityComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
