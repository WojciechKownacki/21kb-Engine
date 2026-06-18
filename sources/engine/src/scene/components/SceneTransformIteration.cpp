#include "scene/components/SceneComponentIteration.hpp"

#include "engine/ecs/Query.hpp"

#include <cstddef>

namespace kb::scene {

namespace {

struct ConstTransformIterationContext {
    ConstTransformVisitor visitor = nullptr;
    void* userContext = nullptr;
};

struct MutableTransformIterationContext {
    MutableTransformVisitor visitor = nullptr;
    void* userContext = nullptr;
};

void VisitTransformBatch(const kb::ecs::QueryBatch<TransformComponent>& batch, void* rawContext) {
    const auto* callbackContext = static_cast<const ConstTransformIterationContext*>(rawContext);
    const TransformComponent* transforms = batch.Components<0>();
    for (std::size_t row = 0; row < batch.Count(); ++row) {
        const SceneEntity entity = batch.EntityAt(row);
        if (entity.IsValid()) {
            callbackContext->visitor(entity, transforms[row], callbackContext->userContext);
        }
    }
}

void VisitMutableTransformBatch(kb::ecs::MutableQueryBatch<TransformComponent>& batch, void* rawContext) {
    const auto* callbackContext = static_cast<const MutableTransformIterationContext*>(rawContext);
    TransformComponent* transforms = batch.Components<0>();
    for (std::size_t row = 0; row < batch.Count(); ++row) {
        const SceneEntity entity = batch.EntityAt(row);
        if (entity.IsValid()) {
            callbackContext->visitor(entity, transforms[row], callbackContext->userContext);
        }
    }
}

} // namespace

void SceneComponentIteration::ForEachTransform(const kb::ecs::World& world, std::uint64_t transformComponentId, ConstTransformVisitor visitor, void* context) {
    static_cast<void>(transformComponentId);
    if (visitor == nullptr) {
        return;
    }

    kb::ecs::Query<TransformComponent> query = const_cast<kb::ecs::World&>(world).CreateQuery<TransformComponent>();
    if (!query.IsValid()) {
        return;
    }

    kb::ecs::QueryExecutionSettings settings;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    ConstTransformIterationContext callbackContext{ .visitor = visitor, .userContext = context };
    query.ForEachBatch(settings, VisitTransformBatch, &callbackContext);
}

void SceneComponentIteration::ForEachMutableTransform(kb::ecs::World& world, std::uint64_t transformComponentId, MutableTransformVisitor visitor, void* context) {
    static_cast<void>(transformComponentId);
    if (visitor == nullptr) {
        return;
    }

    kb::ecs::Query<TransformComponent> query = world.CreateQuery<TransformComponent>();
    if (!query.IsValid()) {
        return;
    }

    kb::ecs::QueryExecutionSettings settings;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    MutableTransformIterationContext callbackContext{ .visitor = visitor, .userContext = context };
    query.ForEachMutableBatch(settings, VisitMutableTransformBatch, &callbackContext);
}

} // namespace kb::scene
