#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

VisibilityComponent SceneComponentQueryService::Visibility(const Scene& scene, SceneEntity entity) {
    const VisibilityComponent* visibility = TryGetVisibility(scene, entity);
    return visibility == nullptr ? VisibilityComponent{} : *visibility;
}

const VisibilityComponent* SceneComponentQueryService::TryGetVisibility(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Visibility().TryGet(entity) : nullptr;
}

VisibilityComponent* SceneComponentMutationService::TryGetVisibility(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Visibility().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetVisibility(Scene& scene, SceneEntity entity, const VisibilityComponent& visibility) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Visibility().Set(entity, visibility);
        if (visibility.visible) {
            ClearSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Hidden);
        } else {
            SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Hidden);
        }
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkVisibilityModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Visibility().MarkModified(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
