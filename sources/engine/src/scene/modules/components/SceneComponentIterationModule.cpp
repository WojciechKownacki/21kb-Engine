#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneIterationService.hpp"

namespace kb::scene {

SceneComponentVisitors::SceneComponentVisitors(const Scene& scene) noexcept
    : scene_(scene) {}

void SceneComponentVisitors::ForEachCamera(CameraVisitor visitor, void* context) const {
    SceneIterationService::ForEachCamera(scene_, visitor, context);
}

void SceneComponentVisitors::ForEachUpdatedCameraRenderProxy(CameraRenderProxyVisitor visitor, void* context) const {
    SceneIterationService::ForEachUpdatedCameraRenderProxy(scene_, visitor, context);
}

void SceneComponentVisitors::ForEachMeshRenderer(MeshRendererVisitor visitor, void* context) const {
    SceneIterationService::ForEachMeshRenderer(scene_, visitor, context);
}

void SceneComponentVisitors::ForEachVisibleMeshRenderer(MeshRendererVisitor visitor, void* context) const {
    SceneIterationService::ForEachVisibleMeshRenderer(scene_, visitor, context);
}

void SceneComponentVisitors::ForEachUpdatedMeshRendererRenderProxy(MeshRendererRenderProxyVisitor visitor, void* context) const {
    SceneIterationService::ForEachUpdatedMeshRendererRenderProxy(scene_, visitor, context);
}

void SceneComponentVisitors::ForEachVisibleUpdatedMeshRendererRenderProxy(MeshRendererRenderProxyVisitor visitor, void* context) const {
    SceneIterationService::ForEachVisibleUpdatedMeshRendererRenderProxy(scene_, visitor, context);
}

void SceneComponentVisitors::ForEachLight(LightVisitor visitor, void* context) const {
    SceneIterationService::ForEachLight(scene_, visitor, context);
}

void SceneComponentVisitors::ForEachUpdatedLightRenderProxy(LightRenderProxyVisitor visitor, void* context) const {
    SceneIterationService::ForEachUpdatedLightRenderProxy(scene_, visitor, context);
}

} // namespace kb::scene
