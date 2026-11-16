#include "scene/components/ScenePlanarReflectionComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

ScenePlanarReflectionComponentStore::ScenePlanarReflectionComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool ScenePlanarReflectionComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<PlanarReflectionComponent>(world_, entity);
}
const PlanarReflectionComponent* ScenePlanarReflectionComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<PlanarReflectionComponent>(world_, entity);
}
PlanarReflectionComponent* ScenePlanarReflectionComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<PlanarReflectionComponent>(world_, entity);
}
void ScenePlanarReflectionComponentStore::Set(SceneEntity entity, const PlanarReflectionComponent& component) {
    SceneComponentStorageAccess::Set<PlanarReflectionComponent>(world_, entity, component);
}
void ScenePlanarReflectionComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<PlanarReflectionComponent>(world_, entity);
}
void ScenePlanarReflectionComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<PlanarReflectionComponent>(world_, entity);
}

} // namespace kb::scene
