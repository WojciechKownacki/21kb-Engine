#include "scene/components/SceneVisibilityCellComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneVisibilityCellComponentStore::SceneVisibilityCellComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneVisibilityCellComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<VisibilityCellComponent>(world_, entity); }
const VisibilityCellComponent* SceneVisibilityCellComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<VisibilityCellComponent>(world_, entity); }
VisibilityCellComponent* SceneVisibilityCellComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<VisibilityCellComponent>(world_, entity); }
void SceneVisibilityCellComponentStore::Set(SceneEntity entity, const VisibilityCellComponent& component) { SceneComponentStorageAccess::Set<VisibilityCellComponent>(world_, entity, component); }
void SceneVisibilityCellComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<VisibilityCellComponent>(world_, entity); }
void SceneVisibilityCellComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<VisibilityCellComponent>(world_, entity); }

} // namespace kb::scene
