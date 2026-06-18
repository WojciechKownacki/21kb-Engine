#include "scene/components/SceneComponentIteration.hpp"

#include "engine/ecs/Query.hpp"

#include <cstddef>

namespace kb::scene {

namespace {

struct BehaviourIterationContext {
    BehaviourVisitor visitor = nullptr;
    void* userContext = nullptr;
};

void VisitBehaviourBatch(const kb::ecs::QueryBatch<BehaviourComponent>& batch, void* rawContext) {
    const auto* callbackContext = static_cast<const BehaviourIterationContext*>(rawContext);
    const BehaviourComponent* behaviours = batch.Components<0>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        const SceneEntity entity = batch.EntityAt(index);
        if (entity.IsValid()) {
            callbackContext->visitor(entity, behaviours[index], callbackContext->userContext);
        }
    }
}

} // namespace

void SceneComponentIteration::ForEachBehaviour(const kb::ecs::World& world, std::uint64_t behaviourComponentId, BehaviourVisitor visitor, void* context) {
    static_cast<void>(behaviourComponentId);
    if (visitor == nullptr) {
        return;
    }

    kb::ecs::Query<BehaviourComponent> query = const_cast<kb::ecs::World&>(world).CreateQuery<BehaviourComponent>();
    if (!query.IsValid()) {
        return;
    }

    kb::ecs::QueryExecutionSettings settings;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    BehaviourIterationContext callbackContext{ .visitor = visitor, .userContext = context };
    query.ForEachBatch(settings, VisitBehaviourBatch, &callbackContext);
}

} // namespace kb::scene
