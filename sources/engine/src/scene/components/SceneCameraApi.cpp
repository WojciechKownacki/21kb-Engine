#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasCamera(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Cameras().Has(entity);
}

const CameraComponent* SceneComponentQueryService::TryGetCamera(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Cameras().TryGet(entity) : nullptr;
}

CameraComponent* SceneComponentMutationService::TryGetCamera(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Cameras().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetCamera(Scene& scene, SceneEntity entity, const CameraComponent& camera) {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Cameras().Set(entity, camera);
        SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Camera);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::RemoveCamera(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Cameras().Remove(entity);
        ClearSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Camera);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkCameraModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Cameras().MarkModified(entity);
        MarkSceneRenderProxyDirty(state, entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
