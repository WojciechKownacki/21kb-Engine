#include "scene/components/SceneTilemapRendererComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneTilemapRendererComponentStore::SceneTilemapRendererComponentStore(kb::ecs::World& world, std::uint64_t) noexcept
    : world_(&world) {}
bool SceneTilemapRendererComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<TilemapRendererComponent>(world_, entity);
}
const TilemapRendererComponent* SceneTilemapRendererComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<TilemapRendererComponent>(world_, entity);
}
TilemapRendererComponent* SceneTilemapRendererComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<TilemapRendererComponent>(world_, entity);
}
void SceneTilemapRendererComponentStore::Set(SceneEntity entity, const TilemapRendererComponent& component) {
    SceneComponentStorageAccess::Set<TilemapRendererComponent>(world_, entity, component);
}
void SceneTilemapRendererComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<TilemapRendererComponent>(world_, entity);
}
void SceneTilemapRendererComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<TilemapRendererComponent>(world_, entity);
}

} // namespace kb::scene
