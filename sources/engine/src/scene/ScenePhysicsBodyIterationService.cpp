#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIteration.hpp"

namespace kb::scene {

void SceneIterationService::ForEachPhysicsBody(const Scene& scene, PhysicsBodyVisitor visitor, void* context) {
    const SceneState& state = SceneAccess::State(scene);
    SceneComponentIteration::ForEachPhysicsBody(
        state.world,
        state.components.TransformComponentId(),
        state.components.RigidbodyComponentId(),
        state.components.ColliderComponentId(),
        state.physicsBodyIterationQuery,
        visitor,
        context);
}

} // namespace kb::scene
