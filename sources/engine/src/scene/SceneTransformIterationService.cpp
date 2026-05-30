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

} // namespace kb::scene
