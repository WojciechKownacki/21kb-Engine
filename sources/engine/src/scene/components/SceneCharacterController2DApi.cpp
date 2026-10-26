#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasCharacterController2D(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.CharacterControllers2D().Has(entity);
}
const CharacterController2DComponent* SceneComponentQueryService::TryGetCharacterController2D(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.CharacterControllers2D().TryGet(entity)
               : nullptr;
}
CharacterController2DComponent* SceneComponentMutationService::TryGetCharacterController2D(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.CharacterControllers2D().TryGet(entity)
               : nullptr;
}
void SceneComponentMutationService::SetCharacterController2D(Scene& scene, SceneEntity entity, const CharacterController2DComponent& component) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.CharacterControllers2D().Set(entity, component);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::RemoveCharacterController2D(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.CharacterControllers2D().Remove(entity);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::MarkCharacterController2DModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.CharacterControllers2D().MarkModified(entity);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
