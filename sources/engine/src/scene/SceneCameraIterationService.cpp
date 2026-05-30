#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIteration.hpp"

namespace kb::scene {

void SceneIterationService::ForEachCamera(const Scene& scene, CameraVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachCamera(state.world, state.components.TransformComponentId(), state.components.CameraComponentId(), visitor, context);
}

} // namespace kb::scene
