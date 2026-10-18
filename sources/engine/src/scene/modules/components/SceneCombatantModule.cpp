#include "engine/scene/SceneCombatantComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneCombatantComponentQueries::SceneCombatantComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneCombatantComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasCombatant(scene_, entity); }
const CombatantComponent* SceneCombatantComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetCombatant(scene_, entity); }

SceneCombatantComponents::SceneCombatantComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneCombatantComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasCombatant(scene_, entity); }
const CombatantComponent* SceneCombatantComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetCombatant(scene_, entity); }
CombatantComponent* SceneCombatantComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetCombatant(scene_, entity); }
bool SceneCombatantComponents::Set(SceneEntity entity, const CombatantComponent& combatant) { return SceneComponentMutationService::SetCombatant(scene_, entity, combatant); }
void SceneCombatantComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveCombatant(scene_, entity); }
void SceneCombatantComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkCombatantModified(scene_, entity); }

} // namespace kb::scene
