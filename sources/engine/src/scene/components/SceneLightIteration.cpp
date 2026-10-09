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
    SceneComponentIterationQueries& cachedQueries,
    LightVisitor visitor,
    void* context) {
    static_cast<void>(transformComponentId);
    static_cast<void>(lightComponentId);
    if (visitor == nullptr) {
        return;
    }

    LightIterationContext callbackContext{
        .visitor = visitor,
        .userContext = context,
    };
    ForEachCachedQueryRange(world, cachedQueries.lights, [&callbackContext](const kb::ecs::UnsafeHotChunk<LightComponent, TransformComponent>& batch) {
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
