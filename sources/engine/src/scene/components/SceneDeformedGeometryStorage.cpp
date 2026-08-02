#include "scene/components/SceneDeformedGeometryComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneDeformedGeometryComponentStore::SceneDeformedGeometryComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) { static_cast<void>(componentId); }
bool SceneDeformedGeometryComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<DrawD3DeformedGeometryComponent>(world_, entity); }
const DrawD3DeformedGeometryComponent* SceneDeformedGeometryComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<DrawD3DeformedGeometryComponent>(world_, entity); }
DrawD3DeformedGeometryComponent* SceneDeformedGeometryComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<DrawD3DeformedGeometryComponent>(world_, entity); }
void SceneDeformedGeometryComponentStore::Set(SceneEntity entity, const DrawD3DeformedGeometryComponent& geometry) { SceneComponentStorageAccess::Set<DrawD3DeformedGeometryComponent>(world_, entity, geometry); }
void SceneDeformedGeometryComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<DrawD3DeformedGeometryComponent>(world_, entity); }
void SceneDeformedGeometryComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<DrawD3DeformedGeometryComponent>(world_, entity); }

} // namespace kb::scene
