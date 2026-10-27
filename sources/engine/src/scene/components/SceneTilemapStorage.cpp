#include "scene/components/SceneTilemapComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneTilemapComponentStore::SceneTilemapComponentStore(kb::ecs::World& world, std::uint64_t) noexcept
    : world_(&world) {}
bool SceneTilemapComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<TilemapComponent>(world_, entity);
}
const TilemapComponent* SceneTilemapComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<TilemapComponent>(world_, entity);
}
TilemapComponent* SceneTilemapComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<TilemapComponent>(world_, entity);
}
void SceneTilemapComponentStore::Set(SceneEntity entity, const TilemapComponent& component) {
    SceneComponentStorageAccess::Set<TilemapComponent>(world_, entity, component);
}
void SceneTilemapComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<TilemapComponent>(world_, entity);
}
void SceneTilemapComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<TilemapComponent>(world_, entity);
}

} // namespace kb::scene
