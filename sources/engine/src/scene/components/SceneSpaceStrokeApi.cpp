#include "engine/scene/SceneSpaceStrokeComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasSpaceStroke(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.SpaceStrokes().Has(entity); }
const SpaceStrokeComponent* SceneComponentQueryService::TryGetSpaceStroke(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.SpaceStrokes().TryGet(entity) : nullptr; }
SpaceStrokeComponent* SceneComponentMutationService::TryGetSpaceStroke(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.SpaceStrokes().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetSpaceStroke(Scene& scene, SceneEntity entity, const SpaceStrokeComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.SpaceStrokes().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveSpaceStroke(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.SpaceStrokes().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkSpaceStrokeModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.SpaceStrokes().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneSpaceStrokeComponentQueries::SceneSpaceStrokeComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneSpaceStrokeComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasSpaceStroke(scene_, entity); }
const SpaceStrokeComponent* SceneSpaceStrokeComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetSpaceStroke(scene_, entity); }
void SceneSpaceStrokeComponentQueries::ForEach(SpaceStrokeVisitor visitor, void* context) const { SceneAccess::State(scene_).componentStorage.SpaceStrokes().ForEach(visitor, context); }
SceneSpaceStrokeComponents::SceneSpaceStrokeComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneSpaceStrokeComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasSpaceStroke(scene_, entity); }
const SpaceStrokeComponent* SceneSpaceStrokeComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetSpaceStroke(scene_, entity); }
SpaceStrokeComponent* SceneSpaceStrokeComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetSpaceStroke(scene_, entity); }
void SceneSpaceStrokeComponents::Set(SceneEntity entity, const SpaceStrokeComponent& component) { SceneComponentMutationService::SetSpaceStroke(scene_, entity, component); }
void SceneSpaceStrokeComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveSpaceStroke(scene_, entity); }
void SceneSpaceStrokeComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkSpaceStrokeModified(scene_, entity); }

} // namespace kb::scene
