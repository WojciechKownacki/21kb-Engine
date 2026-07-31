#include "scene/components/SceneWorldBackdropComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneWorldBackdropComponentStore::SceneWorldBackdropComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneWorldBackdropComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<WorldBackdropComponent>(world_, entity); }
const WorldBackdropComponent* SceneWorldBackdropComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<WorldBackdropComponent>(world_, entity); }
WorldBackdropComponent* SceneWorldBackdropComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<WorldBackdropComponent>(world_, entity); }
void SceneWorldBackdropComponentStore::Set(SceneEntity entity, const WorldBackdropComponent& component) { SceneComponentStorageAccess::Set<WorldBackdropComponent>(world_, entity, component); }
void SceneWorldBackdropComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<WorldBackdropComponent>(world_, entity); }
void SceneWorldBackdropComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<WorldBackdropComponent>(world_, entity); }

} // namespace kb::scene
