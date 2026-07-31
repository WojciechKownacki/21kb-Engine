#include "scene/components/SceneDetailSwitchComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneDetailSwitchComponentStore::SceneDetailSwitchComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneDetailSwitchComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<SceneDetailSwitchComponent>(world_, entity); }
const SceneDetailSwitchComponent* SceneDetailSwitchComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<SceneDetailSwitchComponent>(world_, entity); }
SceneDetailSwitchComponent* SceneDetailSwitchComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<SceneDetailSwitchComponent>(world_, entity); }
void SceneDetailSwitchComponentStore::Set(SceneEntity entity, const SceneDetailSwitchComponent& component) { SceneComponentStorageAccess::Set<SceneDetailSwitchComponent>(world_, entity, component); }
void SceneDetailSwitchComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<SceneDetailSwitchComponent>(world_, entity); }
void SceneDetailSwitchComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<SceneDetailSwitchComponent>(world_, entity); }

} // namespace kb::scene
