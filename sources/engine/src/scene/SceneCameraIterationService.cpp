#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIteration.hpp"

#include <algorithm>
#include <span>

namespace kb::scene {
namespace {

void ForEachUpdatedCameraRenderProxyImpl(const Scene& scene, CameraRenderProxyVisitor visitor, void* context) {
    if (visitor == nullptr) {
        return;
    }

    const SceneState& state = SceneAccess::State(scene);
    for (const std::size_t proxyIndex : state.transformRenderProxyCameraIndices) {
        if (proxyIndex >= state.transformRenderProxyUpdateEntities.size() ||
            proxyIndex >= state.transformRenderProxyWorldAffine3x4.size()) {
            continue;
        }
        const SceneEntity entity = state.transformRenderProxyUpdateEntities[proxyIndex];
        const CameraComponent* camera = state.componentStorage.Cameras().TryGet(entity);
        if (camera != nullptr) {
            visitor(entity, state.transformRenderProxyWorldAffine3x4[proxyIndex], *camera, context);
        }
    }
}

} // namespace

void SceneIterationService::ForEachCamera(const Scene& scene, CameraVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachCamera(
        state.world,
        state.components.TransformComponentId(),
        state.components.CameraComponentId(),
        state.cameraIterationQuery,
        visitor,
        context);
}

void SceneIterationService::ForEachUpdatedCameraRenderProxy(const Scene& scene, CameraRenderProxyVisitor visitor, void* context) {
    ForEachUpdatedCameraRenderProxyImpl(scene, visitor, context);
}

} // namespace kb::scene
