#include "scene/components/SceneComponentIteration.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"

#include <cstddef>

namespace kb::scene {
namespace {

struct CameraIterationContext {
    CameraVisitor visitor = nullptr;
    void* userContext = nullptr;
};

} // namespace

void SceneComponentIteration::ForEachCamera(const kb::ecs::World& world, std::uint64_t transformComponentId,
                                            std::uint64_t cameraComponentId, SceneComponentIterationQueries& cachedQueries,
                                            CameraVisitor visitor, void* context) {
    static_cast<void>(transformComponentId);
    static_cast<void>(cameraComponentId);
    if (visitor == nullptr) {
        return;
    }

    CameraIterationContext callbackContext{
        .visitor = visitor,
        .userContext = context,
    };
    ForEachCachedQueryRange(
        world, cachedQueries.cameras, [&callbackContext](const kb::ecs::UnsafeHotChunk<CameraComponent, TransformComponent>& batch) {
            const CameraComponent* cameras = batch.Components<0>();
            const TransformComponent* transforms = batch.Components<1>();
            for (std::size_t row = 0U; row < batch.Count(); ++row) {
                const SceneEntity entity = batch.EntityAt(row);
                if (entity.IsValid()) {
                    callbackContext.visitor(entity, transforms[row], cameras[row], callbackContext.userContext);
                }
            }
        });
}

} // namespace kb::scene
