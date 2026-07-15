#include "scene/components/SceneColliderComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneColliderComponentStore::SceneColliderComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) {
    static_cast<void>(componentId);
}

bool SceneColliderComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<ColliderComponent>(world_, entity);
}

const ColliderComponent* SceneColliderComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<ColliderComponent>(world_, entity);
}

ColliderComponent* SceneColliderComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<ColliderComponent>(world_, entity);
}

void SceneColliderComponentStore::Set(SceneEntity entity, const ColliderComponent& collider) {
    SceneComponentStorageAccess::Set<ColliderComponent>(world_, entity, collider);
}

void SceneColliderComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<ColliderComponent>(world_, entity);
}

void SceneColliderComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<ColliderComponent>(world_, entity);
}

} // namespace kb::scene
