#include "scene/components/SceneMeshRendererIterationDispatcher.hpp"

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
    ecs_query_t*& cachedQuery,
    MeshRendererVisitor visitor,
    void* context) {
    static_cast<void>(transformComponentId);
    static_cast<void>(visibilityComponentId);
    static_cast<void>(meshRendererComponentId);
    static_cast<void>(cachedQuery);
    if (visitor == nullptr) {
        return;
    }

    kb::ecs::Query<MeshRendererComponent, TransformComponent> query =
        const_cast<kb::ecs::World&>(world).CreateQuery<MeshRendererComponent, TransformComponent>();
    if (!query.IsValid()) {
        return;
    }

    kb::ecs::QueryExecutionSettings settings;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    RendererIterationContext callbackContext{
        .world = &world,
        .visitor = visitor,
        .userContext = context,
        .visibleOnly = visibleOnly,
    };
    kb::ecs::UnsafeHotReadQuery<MeshRendererComponent, TransformComponent> hotQuery;
    if (!hotQuery.Rebuild(query, settings)) {
        return;
    }
    hotQuery.ForEachRange(0U, [&callbackContext](const kb::ecs::UnsafeHotChunk<MeshRendererComponent, TransformComponent>& batch) {
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
