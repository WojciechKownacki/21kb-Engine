#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasWorldBackdrop(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.WorldBackdrops().Has(entity); }
const WorldBackdropComponent* SceneComponentQueryService::TryGetWorldBackdrop(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.WorldBackdrops().TryGet(entity) : nullptr; }
WorldBackdropComponent* SceneComponentMutationService::TryGetWorldBackdrop(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.WorldBackdrops().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetWorldBackdrop(Scene& scene, SceneEntity entity, const WorldBackdropComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.WorldBackdrops().Set(entity, component); MarkSceneRenderProxyDirty(state, entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveWorldBackdrop(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.WorldBackdrops().Remove(entity); MarkSceneRenderProxyDirty(state, entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkWorldBackdropModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.WorldBackdrops().MarkModified(entity); MarkSceneRenderProxyDirty(state, entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneWorldBackdropComponentQueries::SceneWorldBackdropComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneWorldBackdropComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasWorldBackdrop(scene_, entity); }
const WorldBackdropComponent* SceneWorldBackdropComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetWorldBackdrop(scene_, entity); }
SceneWorldBackdropComponents::SceneWorldBackdropComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneWorldBackdropComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasWorldBackdrop(scene_, entity); }
const WorldBackdropComponent* SceneWorldBackdropComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetWorldBackdrop(scene_, entity); }
WorldBackdropComponent* SceneWorldBackdropComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetWorldBackdrop(scene_, entity); }
void SceneWorldBackdropComponents::Set(SceneEntity entity, const WorldBackdropComponent& component) { SceneComponentMutationService::SetWorldBackdrop(scene_, entity, component); }
void SceneWorldBackdropComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveWorldBackdrop(scene_, entity); }
void SceneWorldBackdropComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkWorldBackdropModified(scene_, entity); }

} // namespace kb::scene
