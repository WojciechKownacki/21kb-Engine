#include "scene/components/SceneStreamFocusComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneStreamFocusComponentStore::SceneStreamFocusComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneStreamFocusComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<StreamFocusComponent>(world_, entity); }
const StreamFocusComponent* SceneStreamFocusComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<StreamFocusComponent>(world_, entity); }
StreamFocusComponent* SceneStreamFocusComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<StreamFocusComponent>(world_, entity); }
void SceneStreamFocusComponentStore::Set(SceneEntity entity, const StreamFocusComponent& component) { SceneComponentStorageAccess::Set<StreamFocusComponent>(world_, entity, component); }
void SceneStreamFocusComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<StreamFocusComponent>(world_, entity); }
void SceneStreamFocusComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<StreamFocusComponent>(world_, entity); }

} // namespace kb::scene
