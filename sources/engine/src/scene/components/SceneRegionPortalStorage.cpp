#include "scene/components/SceneRegionPortalComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneRegionPortalComponentStore::SceneRegionPortalComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneRegionPortalComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<SceneRegionPortalComponent>(world_, entity); }
const SceneRegionPortalComponent* SceneRegionPortalComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<SceneRegionPortalComponent>(world_, entity); }
SceneRegionPortalComponent* SceneRegionPortalComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<SceneRegionPortalComponent>(world_, entity); }
void SceneRegionPortalComponentStore::Set(SceneEntity entity, const SceneRegionPortalComponent& component) { SceneComponentStorageAccess::Set<SceneRegionPortalComponent>(world_, entity, component); }
void SceneRegionPortalComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<SceneRegionPortalComponent>(world_, entity); }
void SceneRegionPortalComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<SceneRegionPortalComponent>(world_, entity); }

} // namespace kb::scene
