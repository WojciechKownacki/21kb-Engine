#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIteration.hpp"

namespace kb::scene {

void SceneIterationService::ForEachTransform(const Scene& scene, ConstTransformVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachTransform(state.world, state.components.TransformComponentId(), visitor, context);
}

void SceneIterationService::ForEachMutableTransform(Scene& scene, MutableTransformVisitor visitor, void* context) {
    SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachMutableTransform(state.world, state.components.TransformComponentId(), visitor, context);
}

void SceneIterationService::ForEachCamera(const Scene& scene, CameraVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachCamera(state.world, state.components.TransformComponentId(), state.components.CameraComponentId(), visitor, context);
}

void SceneIterationService::ForEachMeshRenderer(const Scene& scene, MeshRendererVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachMeshRenderer(state.world, state.components.TransformComponentId(), state.components.MeshRendererComponentId(), visitor, context);
}

void SceneIterationService::ForEachVisibleMeshRenderer(const Scene& scene, MeshRendererVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachVisibleMeshRenderer(
        state.world,
        state.components.TransformComponentId(),
        state.components.VisibilityComponentId(),
        state.components.MeshRendererComponentId(),
        visitor,
        context);
}

void SceneIterationService::ForEachLight(const Scene& scene, LightVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachLight(state.world, state.components.TransformComponentId(), state.components.LightComponentId(), visitor, context);
}

} // namespace kb::scene
