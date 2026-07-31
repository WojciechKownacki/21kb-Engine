#include "scene/components/SceneVisibilityBlockerComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneVisibilityBlockerComponentStore::SceneVisibilityBlockerComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneVisibilityBlockerComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<SceneVisibilityBlockerComponent>(world_, entity); }
const SceneVisibilityBlockerComponent* SceneVisibilityBlockerComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<SceneVisibilityBlockerComponent>(world_, entity); }
SceneVisibilityBlockerComponent* SceneVisibilityBlockerComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<SceneVisibilityBlockerComponent>(world_, entity); }
void SceneVisibilityBlockerComponentStore::Set(SceneEntity entity, const SceneVisibilityBlockerComponent& component) { SceneComponentStorageAccess::Set<SceneVisibilityBlockerComponent>(world_, entity, component); }
void SceneVisibilityBlockerComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<SceneVisibilityBlockerComponent>(world_, entity); }
void SceneVisibilityBlockerComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<SceneVisibilityBlockerComponent>(world_, entity); }

} // namespace kb::scene
