#include "scene/components/SceneRigidbodyComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneRigidbodyComponentStore::SceneRigidbodyComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

bool SceneRigidbodyComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, componentId_);
}

const RigidbodyComponent* SceneRigidbodyComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<RigidbodyComponent>(world_, entity, componentId_);
}

RigidbodyComponent* SceneRigidbodyComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<RigidbodyComponent>(world_, entity, componentId_);
}

void SceneRigidbodyComponentStore::Set(SceneEntity entity, const RigidbodyComponent& rigidbody) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, rigidbody);
}

void SceneRigidbodyComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, componentId_);
}

void SceneRigidbodyComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
