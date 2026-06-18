#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIteration.hpp"

namespace kb::scene {

void SceneIterationService::ForEachLight(const Scene& scene, LightVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachLight(
        state.world,
        state.components.TransformComponentId(),
        state.components.LightComponentId(),
        state.lightIterationQuery,
        visitor,
        context);
}

} // namespace kb::scene
