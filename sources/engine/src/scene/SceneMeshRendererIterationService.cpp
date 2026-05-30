#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIteration.hpp"

namespace kb::scene {

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

} // namespace kb::scene
