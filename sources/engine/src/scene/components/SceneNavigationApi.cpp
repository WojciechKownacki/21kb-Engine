#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

#define KB_NAVIGATION_API(Name, Type) \
bool SceneComponentQueryService::Has##Name(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Navigation().Has##Name(entity); } \
const Type* SceneComponentQueryService::TryGet##Name(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Navigation().TryGet##Name(entity) : nullptr; } \
Type* SceneComponentMutationService::TryGet##Name(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Navigation().TryGet##Name(entity) : nullptr; } \
void SceneComponentMutationService::Set##Name(Scene& scene, SceneEntity entity, const Type& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.Navigation().Set##Name(entity, component); MarkScenePrefabNodeDirty(state, entity); } } \
void SceneComponentMutationService::Remove##Name(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.Navigation().Remove##Name(entity); MarkScenePrefabNodeDirty(state, entity); } } \
void SceneComponentMutationService::Mark##Name##Modified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.Navigation().Mark##Name##Modified(entity); MarkScenePrefabNodeDirty(state, entity); } }

KB_NAVIGATION_API(NavAgent, NavAgent)
KB_NAVIGATION_API(NavObstacle, NavObstacle)

#undef KB_NAVIGATION_API

} // namespace kb::scene
