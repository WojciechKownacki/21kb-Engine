#include "scene/components/SceneMeshRendererIterationDispatcher.hpp"
#include "scene/components/SceneComponentIteration.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <cstddef>

namespace kb::scene {
namespace {

struct RendererIterationContext {
    const kb::ecs::World* world = nullptr;
    MeshRendererVisitor visitor = nullptr;
    void* userContext = nullptr;
    bool visibleOnly = false;
};

} // namespace

void SceneMeshRendererIterationDispatcher::ForEach(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t visibilityComponentId,
    std::uint64_t meshRendererComponentId,
    bool visibleOnly,
    SceneComponentIterationQueries& cachedQueries,
    MeshRendererVisitor visitor,
    void* context) {
    static_cast<void>(transformComponentId);
    static_cast<void>(visibilityComponentId);
    static_cast<void>(meshRendererComponentId);
    if (visitor == nullptr) {
        return;
    }

    RendererIterationContext callbackContext{
        .world = &world,
        .visitor = visitor,
        .userContext = context,
        .visibleOnly = visibleOnly,
    };
    ForEachCachedQueryRange(world, cachedQueries.meshRenderers, [&callbackContext](const kb::ecs::UnsafeHotChunk<MeshRendererComponent, TransformComponent>& batch) {
        const MeshRendererComponent* renderers = batch.Components<0>();
        const TransformComponent* transforms = batch.Components<1>();
        for (std::size_t row = 0U; row < batch.Count(); ++row) {
            const SceneEntity entity = batch.EntityAt(row);
            if (!entity.IsValid()) {
                continue;
            }
            if (callbackContext.visibleOnly) {
                const VisibilityComponent* visibility = callbackContext.world->TryGet<VisibilityComponent>(entity);
                if (visibility != nullptr && !visibility->visible) {
                    continue;
                }
            }
            callbackContext.visitor(entity, transforms[row], renderers[row], callbackContext.userContext);
        }
    });
}

} // namespace kb::scene
