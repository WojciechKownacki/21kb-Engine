#include "scene/components/SceneGridComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneGridComponentStore::SceneGridComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneGridComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<GridComponent>(world_, entity);
}
const GridComponent* SceneGridComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<GridComponent>(world_, entity);
}
GridComponent* SceneGridComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<GridComponent>(world_, entity);
}
void SceneGridComponentStore::Set(SceneEntity entity, const GridComponent& component) {
    SceneComponentStorageAccess::Set<GridComponent>(world_, entity, component);
}
void SceneGridComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<GridComponent>(world_, entity);
}
void SceneGridComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<GridComponent>(world_, entity);
}

} // namespace kb::scene
