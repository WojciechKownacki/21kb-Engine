#include "engine/scene/SceneGeometrySwarmComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasGeometrySwarm(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.GeometrySwarms().Has(entity); }
const GeometrySwarmComponent* SceneComponentQueryService::TryGetGeometrySwarm(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.GeometrySwarms().TryGet(entity) : nullptr; }
GeometrySwarmComponent* SceneComponentMutationService::TryGetGeometrySwarm(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.GeometrySwarms().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetGeometrySwarm(Scene& scene, SceneEntity entity, const GeometrySwarmComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.GeometrySwarms().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveGeometrySwarm(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.GeometrySwarms().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkGeometrySwarmModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.GeometrySwarms().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneGeometrySwarmComponentQueries::SceneGeometrySwarmComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneGeometrySwarmComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasGeometrySwarm(scene_, entity); }
const GeometrySwarmComponent* SceneGeometrySwarmComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetGeometrySwarm(scene_, entity); }
void SceneGeometrySwarmComponentQueries::ForEach(GeometrySwarmVisitor visitor, void* context) const { SceneAccess::State(scene_).componentStorage.GeometrySwarms().ForEach(visitor, context); }
SceneGeometrySwarmComponents::SceneGeometrySwarmComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneGeometrySwarmComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasGeometrySwarm(scene_, entity); }
const GeometrySwarmComponent* SceneGeometrySwarmComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetGeometrySwarm(scene_, entity); }
GeometrySwarmComponent* SceneGeometrySwarmComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetGeometrySwarm(scene_, entity); }
void SceneGeometrySwarmComponents::Set(SceneEntity entity, const GeometrySwarmComponent& component) { SceneComponentMutationService::SetGeometrySwarm(scene_, entity, component); }
void SceneGeometrySwarmComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveGeometrySwarm(scene_, entity); }
void SceneGeometrySwarmComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkGeometrySwarmModified(scene_, entity); }

} // namespace kb::scene
