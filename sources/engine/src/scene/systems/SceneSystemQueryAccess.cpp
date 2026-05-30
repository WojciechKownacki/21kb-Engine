#include "engine/scene/SceneSystemQueryAccess.hpp"

#include "scene/SceneIterationService.hpp"

namespace kb::scene {

SceneSystemQueryAccess::SceneSystemQueryAccess(Scene& scene) noexcept
    : scene_(scene) {}

void SceneSystemQueryAccess::ForEachCamera(CameraVisitor visitor, void* context) const {
    SceneIterationService::ForEachCamera(scene_, visitor, context);
}

void SceneSystemQueryAccess::ForEachMeshRenderer(MeshRendererVisitor visitor, void* context) const {
    SceneIterationService::ForEachMeshRenderer(scene_, visitor, context);
}

void SceneSystemQueryAccess::ForEachVisibleMeshRenderer(MeshRendererVisitor visitor, void* context) const {
    SceneIterationService::ForEachVisibleMeshRenderer(scene_, visitor, context);
}

void SceneSystemQueryAccess::ForEachLight(LightVisitor visitor, void* context) const {
    SceneIterationService::ForEachLight(scene_, visitor, context);
}

} // namespace kb::scene
