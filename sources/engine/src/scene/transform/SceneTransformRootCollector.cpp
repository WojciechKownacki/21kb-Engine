#include "scene/transform/SceneTransformRootCollector.hpp"

#include "ecs/world/WorldInternalAccess.hpp"

#include <flecs.h>

namespace kb::scene {

std::vector<SceneEntity> SceneTransformRootCollector::Collect(const kb::ecs::World& world, std::uint64_t transformComponentId) const {
    std::vector<SceneEntity> roots;
    ecs_iter_t it = ecs_each_id(world.NativeHandle(), transformComponentId);

    while (ecs_each_next(&it)) {
        roots.reserve(roots.size() + static_cast<std::size_t>(it.count));
        for (int32_t i = 0; i < it.count; ++i) {
            const ecs_entity_t entity = it.entities[i];
            if (ecs_get_parent(world.NativeHandle(), entity) == 0) {
                if (const SceneEntity resolved = kb::ecs::WorldInternalAccess::ResolveAliveEntity(world, entity); resolved.IsValid()) {
                    roots.push_back(resolved);
                }
            }
        }
    }

    return roots;
}

} // namespace kb::scene
