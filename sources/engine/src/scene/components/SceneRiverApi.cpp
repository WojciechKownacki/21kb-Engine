#include "engine/scene/SceneRiverComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasRiver(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Rivers().Has(entity); }
const RiverComponent* SceneComponentQueryService::TryGetRiver(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Rivers().TryGet(entity) : nullptr; }
RiverComponent* SceneComponentMutationService::TryGetRiver(Scene& scene, SceneEntity entity) noexcept { SceneHistoryService::NoteObjectChanging(scene, entity); return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Rivers().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetRiver(Scene& scene, SceneEntity entity, const RiverComponent& component) { SceneHistoryService::NoteObjectChanging(scene, entity); if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.Rivers().Set(entity, component); MarkSceneRenderProxyDirty(state, entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveRiver(Scene& scene, SceneEntity entity) noexcept { SceneHistoryService::NoteObjectChanging(scene, entity); if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.Rivers().Remove(entity); MarkSceneRenderProxyDirty(state, entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkRiverModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.Rivers().MarkModified(entity); MarkSceneRenderProxyDirty(state, entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneRiverComponentQueries::SceneRiverComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneRiverComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasRiver(scene_, entity); }
const RiverComponent* SceneRiverComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetRiver(scene_, entity); }
void SceneRiverComponentQueries::ForEach(RiverVisitor visitor, void* context) const { SceneAccess::State(scene_).componentStorage.Rivers().ForEach(visitor, context); }
SceneRiverComponents::SceneRiverComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneRiverComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasRiver(scene_, entity); }
const RiverComponent* SceneRiverComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetRiver(scene_, entity); }
RiverComponent* SceneRiverComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetRiver(scene_, entity); }
void SceneRiverComponents::Set(SceneEntity entity, const RiverComponent& component) { SceneComponentMutationService::SetRiver(scene_, entity, component); }
void SceneRiverComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveRiver(scene_, entity); }
void SceneRiverComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkRiverModified(scene_, entity); }

} // namespace kb::scene
