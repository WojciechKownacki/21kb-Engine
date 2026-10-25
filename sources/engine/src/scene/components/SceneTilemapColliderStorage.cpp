#include "scene/components/SceneTilemapColliderComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneTilemapColliderComponentStore::SceneTilemapColliderComponentStore(kb::ecs::World& world, std::uint64_t) noexcept
    : world_(&world) {}
bool SceneTilemapColliderComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<TilemapColliderComponent>(world_, entity);
}
const TilemapColliderComponent* SceneTilemapColliderComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<TilemapColliderComponent>(world_, entity);
}
TilemapColliderComponent* SceneTilemapColliderComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<TilemapColliderComponent>(world_, entity);
}
void SceneTilemapColliderComponentStore::Set(SceneEntity entity, const TilemapColliderComponent& component) {
    SceneComponentStorageAccess::Set<TilemapColliderComponent>(world_, entity, component);
}
void SceneTilemapColliderComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<TilemapColliderComponent>(world_, entity);
}
void SceneTilemapColliderComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<TilemapColliderComponent>(world_, entity);
}

} // namespace kb::scene
