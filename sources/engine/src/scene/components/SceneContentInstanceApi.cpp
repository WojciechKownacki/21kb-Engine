#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasContentInstance(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.ContentInstances().Has(entity); }
const ContentInstanceComponent* SceneComponentQueryService::TryGetContentInstance(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.ContentInstances().TryGet(entity) : nullptr; }
ContentInstanceComponent* SceneComponentMutationService::TryGetContentInstance(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.ContentInstances().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetContentInstance(Scene& scene, SceneEntity entity, const ContentInstanceComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.ContentInstances().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveContentInstance(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.ContentInstances().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkContentInstanceModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.ContentInstances().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

} // namespace kb::scene
