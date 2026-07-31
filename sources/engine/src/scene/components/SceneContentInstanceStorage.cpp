#include "scene/components/SceneContentInstanceComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneContentInstanceComponentStore::SceneContentInstanceComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneContentInstanceComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<ContentInstanceComponent>(world_, entity); }
const ContentInstanceComponent* SceneContentInstanceComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<ContentInstanceComponent>(world_, entity); }
ContentInstanceComponent* SceneContentInstanceComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<ContentInstanceComponent>(world_, entity); }
void SceneContentInstanceComponentStore::Set(SceneEntity entity, const ContentInstanceComponent& component) { SceneComponentStorageAccess::Set<ContentInstanceComponent>(world_, entity, component); }
void SceneContentInstanceComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<ContentInstanceComponent>(world_, entity); }
void SceneContentInstanceComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<ContentInstanceComponent>(world_, entity); }

} // namespace kb::scene
