#include "scene/components/SceneRegionShapeComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneRegionShapeComponentStore::SceneRegionShapeComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) {
    static_cast<void>(componentId);
}

bool SceneRegionShapeComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<RegionShapeComponent>(world_, entity);
}

const RegionShapeComponent* SceneRegionShapeComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<RegionShapeComponent>(world_, entity);
}

RegionShapeComponent* SceneRegionShapeComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<RegionShapeComponent>(world_, entity);
}

void SceneRegionShapeComponentStore::Set(SceneEntity entity, const RegionShapeComponent& shape) {
    SceneComponentStorageAccess::Set<RegionShapeComponent>(world_, entity, shape);
}

void SceneRegionShapeComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<RegionShapeComponent>(world_, entity);
}

void SceneRegionShapeComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<RegionShapeComponent>(world_, entity);
}

} // namespace kb::scene
