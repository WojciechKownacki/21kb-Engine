#include "engine/scene/SceneHistoryRibbonComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasHistoryRibbon(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.HistoryRibbons().Has(entity); }
const HistoryRibbonComponent* SceneComponentQueryService::TryGetHistoryRibbon(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.HistoryRibbons().TryGet(entity) : nullptr; }
HistoryRibbonComponent* SceneComponentMutationService::TryGetHistoryRibbon(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.HistoryRibbons().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetHistoryRibbon(Scene& scene, SceneEntity entity, const HistoryRibbonComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.HistoryRibbons().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveHistoryRibbon(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.HistoryRibbons().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkHistoryRibbonModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.HistoryRibbons().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneHistoryRibbonComponentQueries::SceneHistoryRibbonComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneHistoryRibbonComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasHistoryRibbon(scene_, entity); }
const HistoryRibbonComponent* SceneHistoryRibbonComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetHistoryRibbon(scene_, entity); }
void SceneHistoryRibbonComponentQueries::ForEach(HistoryRibbonVisitor visitor, void* context) const { SceneAccess::State(scene_).componentStorage.HistoryRibbons().ForEach(visitor, context); }
SceneHistoryRibbonComponents::SceneHistoryRibbonComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneHistoryRibbonComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasHistoryRibbon(scene_, entity); }
const HistoryRibbonComponent* SceneHistoryRibbonComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetHistoryRibbon(scene_, entity); }
HistoryRibbonComponent* SceneHistoryRibbonComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetHistoryRibbon(scene_, entity); }
void SceneHistoryRibbonComponents::Set(SceneEntity entity, const HistoryRibbonComponent& component) { SceneComponentMutationService::SetHistoryRibbon(scene_, entity, component); }
void SceneHistoryRibbonComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveHistoryRibbon(scene_, entity); }
void SceneHistoryRibbonComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkHistoryRibbonModified(scene_, entity); }

} // namespace kb::scene
