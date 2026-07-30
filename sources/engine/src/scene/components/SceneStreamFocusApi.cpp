#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasStreamFocus(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.StreamFocuses().Has(entity); }
const StreamFocusComponent* SceneComponentQueryService::TryGetStreamFocus(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.StreamFocuses().TryGet(entity) : nullptr; }
StreamFocusComponent* SceneComponentMutationService::TryGetStreamFocus(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.StreamFocuses().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetStreamFocus(Scene& scene, SceneEntity entity, const StreamFocusComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.StreamFocuses().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveStreamFocus(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.StreamFocuses().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkStreamFocusModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state=SceneAccess::State(scene); state.componentStorage.StreamFocuses().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

} // namespace kb::scene
