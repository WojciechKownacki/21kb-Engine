#include "scene/components/SceneCombatantComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneCombatantComponentStore::SceneCombatantComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) { static_cast<void>(componentId); }

bool SceneCombatantComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<CombatantComponent>(world_, entity);
}
const CombatantComponent* SceneCombatantComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<CombatantComponent>(world_, entity);
}
CombatantComponent* SceneCombatantComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<CombatantComponent>(world_, entity);
}
void SceneCombatantComponentStore::Set(SceneEntity entity, const CombatantComponent& combatant) {
    SceneComponentStorageAccess::Set<CombatantComponent>(world_, entity, combatant);
}
void SceneCombatantComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<CombatantComponent>(world_, entity);
}
void SceneCombatantComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<CombatantComponent>(world_, entity);
}

} // namespace kb::scene
