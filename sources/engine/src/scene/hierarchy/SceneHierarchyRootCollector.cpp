#include "scene/hierarchy/SceneHierarchyRootCollector.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <flecs.h>

#include <algorithm>
#include <cstddef>

namespace kb::scene {

std::vector<SceneEntity> SceneHierarchyRootCollector::Roots(const kb::ecs::World& world, std::uint64_t transformComponentId) {
    static_cast<void>(transformComponentId);
    std::vector<SceneEntity> roots;
    kb::ecs::Query<TransformComponent> query = const_cast<kb::ecs::World&>(world).CreateQuery<TransformComponent>();
    if (!query.IsValid()) {
        return roots;
    }

    struct Context {
        const kb::ecs::World* world = nullptr;
        std::vector<SceneEntity>* roots = nullptr;
    } context{ .world = &world, .roots = &roots };

    kb::ecs::QueryExecutionSettings settings;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    kb::ecs::UnsafeHotReadQuery<TransformComponent> hotQuery;
    if (!hotQuery.Rebuild(query, settings)) {
        return roots;
    }
    hotQuery.ForEachRange(settings.maxBatchSize, [&context](const auto& batch) {
        context.roots->reserve(context.roots->size() + batch.Count());
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            const SceneEntity entity = batch.EntityAt(index);
            if (entity.IsValid() && ecs_get_parent(context.world->NativeHandle(), kb::ecs::FlecsEntityId(entity)) == 0) {
                context.roots->push_back(entity);
            }
        }
    });

    std::ranges::sort(roots, [](SceneEntity left, SceneEntity right) {
        return left.Id() < right.Id();
    });
    return roots;
}

} // namespace kb::scene
