#include "scene/components/SceneComponentIteration.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"

#include <cstddef>

namespace kb::scene {
namespace {

struct LightIterationContext {
    LightVisitor visitor = nullptr;
    void* userContext = nullptr;
};

} // namespace

void SceneComponentIteration::ForEachLight(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t lightComponentId,
    ecs_query_t*& cachedQuery,
    LightVisitor visitor,
    void* context) {
    static_cast<void>(transformComponentId);
    static_cast<void>(lightComponentId);
    static_cast<void>(cachedQuery);
    if (visitor == nullptr) {
        return;
    }

    kb::ecs::Query<LightComponent, TransformComponent> query =
        const_cast<kb::ecs::World&>(world).CreateQuery<LightComponent, TransformComponent>();
    if (!query.IsValid()) {
        return;
    }

    kb::ecs::QueryExecutionSettings settings;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    LightIterationContext callbackContext{
        .visitor = visitor,
        .userContext = context,
    };
    kb::ecs::UnsafeHotReadQuery<LightComponent, TransformComponent> hotQuery;
    if (!hotQuery.Rebuild(query, settings)) {
        return;
    }
    hotQuery.ForEachRange(0U, [&callbackContext](const kb::ecs::UnsafeHotChunk<LightComponent, TransformComponent>& batch) {
        const LightComponent* lights = batch.Components<0>();
        const TransformComponent* transforms = batch.Components<1>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) {
            const SceneEntity entity = batch.EntityAt(row);
            if (entity.IsValid()) {
                callbackContext.visitor(entity, transforms[row], lights[row], callbackContext.userContext);
            }
        }
    });
}

} // namespace kb::scene
