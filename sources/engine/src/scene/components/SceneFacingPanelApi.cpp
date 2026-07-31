#include "engine/scene/SceneFacingPanelComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasFacingPanel(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.FacingPanels().Has(entity); }
const FacingPanelComponent* SceneComponentQueryService::TryGetFacingPanel(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.FacingPanels().TryGet(entity) : nullptr; }
FacingPanelComponent* SceneComponentMutationService::TryGetFacingPanel(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.FacingPanels().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetFacingPanel(Scene& scene, SceneEntity entity, const FacingPanelComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.FacingPanels().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveFacingPanel(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.FacingPanels().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkFacingPanelModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.FacingPanels().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneFacingPanelComponentQueries::SceneFacingPanelComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneFacingPanelComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasFacingPanel(scene_, entity); }
const FacingPanelComponent* SceneFacingPanelComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetFacingPanel(scene_, entity); }
void SceneFacingPanelComponentQueries::ForEach(FacingPanelVisitor visitor, void* context) const { SceneAccess::State(scene_).componentStorage.FacingPanels().ForEach(visitor, context); }
SceneFacingPanelComponents::SceneFacingPanelComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneFacingPanelComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasFacingPanel(scene_, entity); }
const FacingPanelComponent* SceneFacingPanelComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetFacingPanel(scene_, entity); }
FacingPanelComponent* SceneFacingPanelComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetFacingPanel(scene_, entity); }
void SceneFacingPanelComponents::Set(SceneEntity entity, const FacingPanelComponent& component) { SceneComponentMutationService::SetFacingPanel(scene_, entity, component); }
void SceneFacingPanelComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveFacingPanel(scene_, entity); }
void SceneFacingPanelComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkFacingPanelModified(scene_, entity); }

} // namespace kb::scene
