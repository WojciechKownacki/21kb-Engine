#include "scene/components/SceneComponentIteration.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"

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
    kb::ecs::UnsafeHotReadQuery<TransformComponent> hotQuery;
    if (!hotQuery.Rebuild(query, settings)) {
        return;
    }
    hotQuery.ForEachRange(0U, [&callbackContext](const kb::ecs::UnsafeHotChunk<TransformComponent>& batch) {
        const TransformComponent* transforms = batch.Components<0>();
        for (std::size_t row = 0; row < batch.Count(); ++row) {
            const SceneEntity entity = batch.EntityAt(row);
            if (entity.IsValid()) {
                callbackContext.visitor(entity, transforms[row], callbackContext.userContext);
            }
        }
    });
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
    kb::ecs::UnsafeHotQuery<TransformComponent> hotQuery;
    if (!hotQuery.Rebuild(query, settings)) {
        return;
    }
    hotQuery.ForEachMutableRange(0U, [&callbackContext](kb::ecs::UnsafeHotMutableChunk<TransformComponent>& batch) {
        TransformComponent* transforms = batch.Components<0>();
        for (std::size_t row = 0; row < batch.Count(); ++row) {
            const SceneEntity entity = batch.EntityAt(row);
            if (entity.IsValid()) {
                callbackContext.visitor(entity, transforms[row], callbackContext.userContext);
            }
        }
    });
    auto& nativeStorage = const_cast<kb::ecs::NativeArchetypeStorage&>(world.NativeStorage());
    hotQuery.MarkCachedRangesDirty(nativeStorage);
}

} // namespace kb::scene
