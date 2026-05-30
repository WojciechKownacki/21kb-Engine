#include "engine/scene/Scene.hpp"

#include "scene/components/SceneComponentIteration.hpp"
#include "scene/components/SceneComponentRegistry.hpp"

namespace kb::scene {

void Scene::ForEachTransform(ConstTransformVisitor visitor, void* context) const {
    SceneComponentIteration::ForEachTransform(world_, components_->TransformComponentId(), visitor, context);
}

void Scene::ForEachMutableTransform(MutableTransformVisitor visitor, void* context) {
    SceneComponentIteration::ForEachMutableTransform(world_, components_->TransformComponentId(), visitor, context);
}

void Scene::ForEachCamera(CameraVisitor visitor, void* context) const {
    SceneComponentIteration::ForEachCamera(world_, components_->TransformComponentId(), components_->CameraComponentId(), visitor, context);
}

void Scene::ForEachMeshRenderer(MeshRendererVisitor visitor, void* context) const {
    SceneComponentIteration::ForEachMeshRenderer(world_, components_->TransformComponentId(), components_->MeshRendererComponentId(), visitor, context);
}

void Scene::ForEachVisibleMeshRenderer(MeshRendererVisitor visitor, void* context) const {
    SceneComponentIteration::ForEachVisibleMeshRenderer(
        world_,
        components_->TransformComponentId(),
        components_->VisibilityComponentId(),
        components_->MeshRendererComponentId(),
        visitor,
        context);
}

void Scene::ForEachLight(LightVisitor visitor, void* context) const {
    SceneComponentIteration::ForEachLight(world_, components_->TransformComponentId(), components_->LightComponentId(), visitor, context);
}

} // namespace kb::scene
