#include "scene/components/SceneCollider2DComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneCollider2DComponentStore::SceneCollider2DComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneCollider2DComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<Collider2DComponent>(world_, entity);
}
const Collider2DComponent* SceneCollider2DComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<Collider2DComponent>(world_, entity);
}
Collider2DComponent* SceneCollider2DComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<Collider2DComponent>(world_, entity);
}
void SceneCollider2DComponentStore::Set(SceneEntity entity, const Collider2DComponent& component) {
    SceneComponentStorageAccess::Set<Collider2DComponent>(world_, entity, component);
}
void SceneCollider2DComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<Collider2DComponent>(world_, entity);
}
void SceneCollider2DComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<Collider2DComponent>(world_, entity);
}

} // namespace kb::scene
