#include "scene/components/SceneVisibilityComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneVisibilityComponentStore::SceneVisibilityComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world)
    , componentId_(componentId) {}

const VisibilityComponent* SceneVisibilityComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<VisibilityComponent>(world_, entity);
}

VisibilityComponent* SceneVisibilityComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<VisibilityComponent>(world_, entity);
}

void SceneVisibilityComponentStore::Set(SceneEntity entity, const VisibilityComponent& visibility) {
    SceneComponentStorageAccess::Set<VisibilityComponent>(world_, entity, visibility);
}

void SceneVisibilityComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<VisibilityComponent>(world_, entity);
}

} // namespace kb::scene
