#include "engine/scene/SceneSurfaceCastComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasSurfaceCast(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.SurfaceCasts().Has(entity); }
const SurfaceCastComponent* SceneComponentQueryService::TryGetSurfaceCast(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.SurfaceCasts().TryGet(entity) : nullptr; }
SurfaceCastComponent* SceneComponentMutationService::TryGetSurfaceCast(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.SurfaceCasts().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetSurfaceCast(Scene& scene, SceneEntity entity, const SurfaceCastComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.SurfaceCasts().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveSurfaceCast(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.SurfaceCasts().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkSurfaceCastModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.SurfaceCasts().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneSurfaceCastComponentQueries::SceneSurfaceCastComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneSurfaceCastComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasSurfaceCast(scene_, entity); }
const SurfaceCastComponent* SceneSurfaceCastComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetSurfaceCast(scene_, entity); }
void SceneSurfaceCastComponentQueries::ForEach(SurfaceCastVisitor visitor, void* context) const { SceneAccess::State(scene_).componentStorage.SurfaceCasts().ForEach(visitor, context); }
SceneSurfaceCastComponents::SceneSurfaceCastComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneSurfaceCastComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasSurfaceCast(scene_, entity); }
const SurfaceCastComponent* SceneSurfaceCastComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetSurfaceCast(scene_, entity); }
SurfaceCastComponent* SceneSurfaceCastComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetSurfaceCast(scene_, entity); }
void SceneSurfaceCastComponents::Set(SceneEntity entity, const SurfaceCastComponent& component) { SceneComponentMutationService::SetSurfaceCast(scene_, entity, component); }
void SceneSurfaceCastComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveSurfaceCast(scene_, entity); }
void SceneSurfaceCastComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkSurfaceCastModified(scene_, entity); }

} // namespace kb::scene
