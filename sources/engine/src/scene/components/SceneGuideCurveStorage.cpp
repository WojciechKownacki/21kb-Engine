#include "scene/components/SceneGuideCurveComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneGuideCurveComponentStore::SceneGuideCurveComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneGuideCurveComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<GuideCurveComponent>(world_, entity); }
const GuideCurveComponent* SceneGuideCurveComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<GuideCurveComponent>(world_, entity); }
GuideCurveComponent* SceneGuideCurveComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<GuideCurveComponent>(world_, entity); }
void SceneGuideCurveComponentStore::Set(SceneEntity entity, const GuideCurveComponent& curve) { SceneComponentStorageAccess::Set<GuideCurveComponent>(world_, entity, curve); }
void SceneGuideCurveComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<GuideCurveComponent>(world_, entity); }
void SceneGuideCurveComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<GuideCurveComponent>(world_, entity); }

} // namespace kb::scene
