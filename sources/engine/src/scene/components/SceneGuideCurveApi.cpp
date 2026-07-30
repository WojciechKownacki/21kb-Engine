#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasGuideCurve(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.GuideCurves().Has(entity); }
const GuideCurveComponent* SceneComponentQueryService::TryGetGuideCurve(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.GuideCurves().TryGet(entity) : nullptr; }
GuideCurveComponent* SceneComponentMutationService::TryGetGuideCurve(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.GuideCurves().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetGuideCurve(Scene& scene, SceneEntity entity, const GuideCurveComponent& curve) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.GuideCurves().Set(entity, curve); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveGuideCurve(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.GuideCurves().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkGuideCurveModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.GuideCurves().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

} // namespace kb::scene
