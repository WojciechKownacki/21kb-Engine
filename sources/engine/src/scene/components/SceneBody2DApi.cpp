#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasBody2D(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Bodies2D().Has(entity);
}
const Body2DComponent* SceneComponentQueryService::TryGetBody2D(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.Bodies2D().TryGet(entity)
               : nullptr;
}
Body2DComponent* SceneComponentMutationService::TryGetBody2D(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.Bodies2D().TryGet(entity)
               : nullptr;
}
void SceneComponentMutationService::SetBody2D(Scene& scene, SceneEntity entity, const Body2DComponent& component) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Bodies2D().Set(entity, component);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::RemoveBody2D(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Bodies2D().Remove(entity);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::MarkBody2DModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Bodies2D().MarkModified(entity);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
