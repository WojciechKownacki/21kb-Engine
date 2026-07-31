#include "scene/components/SceneAmbientRadianceComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneAmbientRadianceComponentStore::SceneAmbientRadianceComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneAmbientRadianceComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<AmbientRadianceComponent>(world_, entity); }
const AmbientRadianceComponent* SceneAmbientRadianceComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<AmbientRadianceComponent>(world_, entity); }
AmbientRadianceComponent* SceneAmbientRadianceComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<AmbientRadianceComponent>(world_, entity); }
void SceneAmbientRadianceComponentStore::Set(SceneEntity entity, const AmbientRadianceComponent& component) { SceneComponentStorageAccess::Set<AmbientRadianceComponent>(world_, entity, component); }
void SceneAmbientRadianceComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<AmbientRadianceComponent>(world_, entity); }
void SceneAmbientRadianceComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<AmbientRadianceComponent>(world_, entity); }

} // namespace kb::scene
