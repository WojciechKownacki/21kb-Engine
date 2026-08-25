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
        VisibilityComponent normalized = visibility;
        if (!IsVisibilityModeValid(normalized.mode)) {
            normalized.mode = normalized.visible ? VisibilityMode::Visible : VisibilityMode::Hidden;
        }
        if (!normalized.visible) {
            normalized.mode = VisibilityMode::Hidden;
        }
        normalized.visible = normalized.mode != VisibilityMode::Hidden;
        state.componentStorage.Visibility().Set(entity, normalized);
        if (normalized.mode != VisibilityMode::Hidden) {
            ClearSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Hidden);
        } else {
            SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Hidden);
        }
        MarkSceneRenderProxySubtreeDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkVisibilityModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        VisibilityComponent* visibility = state.componentStorage.Visibility().TryGet(entity);
        if (visibility == nullptr) {
            return;
        }
        if (!IsVisibilityModeValid(visibility->mode)) {
            visibility->mode = visibility->visible ? VisibilityMode::Visible : VisibilityMode::Hidden;
        }
        if (!visibility->visible) {
            visibility->mode = VisibilityMode::Hidden;
        }
        visibility->visible = visibility->mode != VisibilityMode::Hidden;
        if (visibility->mode != VisibilityMode::Hidden) {
            ClearSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Hidden);
        } else {
            SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Hidden);
        }
        state.componentStorage.Visibility().MarkModified(entity);
        MarkSceneRenderProxySubtreeDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
