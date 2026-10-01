#include "scene/SceneRenderProxyComponentMask.hpp"

namespace kb::scene {

void RefreshSceneRenderProxyComponentMask(SceneState& state, SceneEntity entity) {
    ClearSceneRenderProxyComponentMask(state, entity);
    const auto& storage = state.componentStorage;
    if (storage.MeshRenderers().Has(entity)) {
        SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::MeshRenderer);
    }
    if (storage.Cameras().Has(entity)) {
        SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Camera);
    }
    if (storage.Lights().Has(entity)) {
        SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Light);
    }
    const auto* visibility = storage.Visibility().TryGet(entity);
    if (visibility != nullptr && (visibility->mode == VisibilityMode::Hidden || !visibility->visible)) {
        SetSceneRenderProxyComponentMask(state, entity, SceneRenderProxyComponentMask::Hidden);
    }
}

} // namespace kb::scene
