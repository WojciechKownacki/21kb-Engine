#include "scene/components/SceneRigidbodyComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneRigidbodyComponentStore::SceneRigidbodyComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) {
    static_cast<void>(componentId);
}

bool SceneRigidbodyComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<RigidbodyComponent>(world_, entity);
}

const RigidbodyComponent* SceneRigidbodyComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<RigidbodyComponent>(world_, entity);
}

RigidbodyComponent* SceneRigidbodyComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<RigidbodyComponent>(world_, entity);
}

void SceneRigidbodyComponentStore::Set(SceneEntity entity, const RigidbodyComponent& rigidbody) {
    SceneComponentStorageAccess::Set<RigidbodyComponent>(world_, entity, rigidbody);
}

void SceneRigidbodyComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<RigidbodyComponent>(world_, entity);
}

void SceneRigidbodyComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<RigidbodyComponent>(world_, entity);
}

} // namespace kb::scene
