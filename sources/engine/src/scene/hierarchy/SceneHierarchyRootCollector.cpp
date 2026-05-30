#include "scene/hierarchy/SceneHierarchyRootCollector.hpp"

#include <flecs.h>

#include <algorithm>

namespace kb::scene {

std::vector<SceneEntity> SceneHierarchyRootCollector::Roots(const kb::ecs::World& world, std::uint64_t transformComponentId) {
    std::vector<SceneEntity> roots;
    ecs_iter_t it = ecs_each_id(world.NativeHandle(), transformComponentId);
    while (ecs_each_next(&it)) {
        roots.reserve(roots.size() + static_cast<std::size_t>(std::max(0, it.count)));
        for (int32_t i = 0; i < it.count; ++i) {
            const ecs_entity_t entity = it.entities[i];
            if (ecs_get_parent(world.NativeHandle(), entity) == 0) {
                roots.push_back(SceneEntity{ entity });
            }
        }
    }

    return roots;
}

} // namespace kb::scene
