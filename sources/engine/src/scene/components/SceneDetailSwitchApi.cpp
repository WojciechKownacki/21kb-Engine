#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasDetailSwitch(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.DetailSwitches().Has(entity); }
const SceneDetailSwitchComponent* SceneComponentQueryService::TryGetDetailSwitch(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.DetailSwitches().TryGet(entity) : nullptr; }
SceneDetailSwitchComponent* SceneComponentMutationService::TryGetDetailSwitch(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.DetailSwitches().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetDetailSwitch(Scene& scene, SceneEntity entity, const SceneDetailSwitchComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.DetailSwitches().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveDetailSwitch(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.DetailSwitches().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkDetailSwitchModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.DetailSwitches().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneDetailSwitchComponentQueries::SceneDetailSwitchComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneDetailSwitchComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasDetailSwitch(scene_, entity); }
const SceneDetailSwitchComponent* SceneDetailSwitchComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetDetailSwitch(scene_, entity); }
SceneDetailSwitchComponents::SceneDetailSwitchComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneDetailSwitchComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasDetailSwitch(scene_, entity); }
const SceneDetailSwitchComponent* SceneDetailSwitchComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetDetailSwitch(scene_, entity); }
SceneDetailSwitchComponent* SceneDetailSwitchComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetDetailSwitch(scene_, entity); }
void SceneDetailSwitchComponents::Set(SceneEntity entity, const SceneDetailSwitchComponent& component) { SceneComponentMutationService::SetDetailSwitch(scene_, entity, component); }
void SceneDetailSwitchComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveDetailSwitch(scene_, entity); }
void SceneDetailSwitchComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkDetailSwitchModified(scene_, entity); }

} // namespace kb::scene
