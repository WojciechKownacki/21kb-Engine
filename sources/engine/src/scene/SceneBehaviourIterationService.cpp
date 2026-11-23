#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIteration.hpp"

namespace kb::scene {

void SceneIterationService::ForEachBehaviour(const Scene& scene, BehaviourVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachBehaviour(state.world, state.components.BehaviourComponentId(), visitor, context);
    // The behaviours after an entity's first one come after every first behaviour, in attach order.
    state.componentStorage.Behaviours().ForEachAdditional(visitor, context);
}

} // namespace kb::scene
