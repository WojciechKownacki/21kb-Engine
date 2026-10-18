#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasCombatant(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Combatants().Has(entity);
}
const CombatantComponent* SceneComponentQueryService::TryGetCombatant(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Combatants().TryGet(entity) : nullptr;
}
CombatantComponent* SceneComponentMutationService::TryGetCombatant(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Combatants().TryGet(entity) : nullptr;
}
bool SceneComponentMutationService::SetCombatant(Scene& scene, SceneEntity entity, const CombatantComponent& combatant) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (!SceneEntityService::IsAlive(scene, entity) || !IsCombatantComponentValid(combatant)) return false;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.Combatants().Set(entity, combatant);
    MarkScenePrefabNodeDirty(state, entity);
    return true;
}
void SceneComponentMutationService::RemoveCombatant(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.Combatants().Remove(entity);
    MarkScenePrefabNodeDirty(state, entity);
}
void SceneComponentMutationService::MarkCombatantModified(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.Combatants().MarkModified(entity);
    MarkScenePrefabNodeDirty(state, entity);
}

} // namespace kb::scene
