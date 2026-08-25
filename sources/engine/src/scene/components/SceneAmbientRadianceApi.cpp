#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasAmbientRadiance(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.AmbientRadiances().Has(entity); }
const AmbientRadianceComponent* SceneComponentQueryService::TryGetAmbientRadiance(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AmbientRadiances().TryGet(entity) : nullptr; }
AmbientRadianceComponent* SceneComponentMutationService::TryGetAmbientRadiance(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.AmbientRadiances().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetAmbientRadiance(Scene& scene, SceneEntity entity, const AmbientRadianceComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.AmbientRadiances().Set(entity, component); MarkSceneRenderProxyDirty(state, entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveAmbientRadiance(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.AmbientRadiances().Remove(entity); MarkSceneRenderProxyDirty(state, entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkAmbientRadianceModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.AmbientRadiances().MarkModified(entity); MarkSceneRenderProxyDirty(state, entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneAmbientRadianceComponentQueries::SceneAmbientRadianceComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneAmbientRadianceComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasAmbientRadiance(scene_, entity); }
const AmbientRadianceComponent* SceneAmbientRadianceComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetAmbientRadiance(scene_, entity); }
SceneAmbientRadianceComponents::SceneAmbientRadianceComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneAmbientRadianceComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasAmbientRadiance(scene_, entity); }
const AmbientRadianceComponent* SceneAmbientRadianceComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetAmbientRadiance(scene_, entity); }
AmbientRadianceComponent* SceneAmbientRadianceComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetAmbientRadiance(scene_, entity); }
void SceneAmbientRadianceComponents::Set(SceneEntity entity, const AmbientRadianceComponent& component) { SceneComponentMutationService::SetAmbientRadiance(scene_, entity, component); }
void SceneAmbientRadianceComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveAmbientRadiance(scene_, entity); }
void SceneAmbientRadianceComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkAmbientRadianceModified(scene_, entity); }

} // namespace kb::scene
