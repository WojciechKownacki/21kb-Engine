#include "scene/components/SceneColliderComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneColliderComponentStore::SceneColliderComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

bool SceneColliderComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, componentId_);
}

const ColliderComponent* SceneColliderComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<ColliderComponent>(world_, entity, componentId_);
}

ColliderComponent* SceneColliderComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<ColliderComponent>(world_, entity, componentId_);
}

void SceneColliderComponentStore::Set(SceneEntity entity, const ColliderComponent& collider) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, collider);
}

void SceneColliderComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, componentId_);
}

void SceneColliderComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
