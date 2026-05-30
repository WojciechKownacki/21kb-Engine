#include "scene/hierarchy/SceneHierarchyReader.hpp"

#include <flecs.h>

#include <algorithm>

namespace kb::scene {

SceneEntity SceneHierarchyReader::Parent(const kb::ecs::World& world, SceneEntity entity) noexcept {
    if (!world.IsAlive(entity)) {
        return {};
    }

    const ecs_entity_t parent = ecs_get_parent(world.NativeHandle(), entity.Id());
    return parent == 0 ? SceneEntity{} : SceneEntity{ parent };
}

std::vector<SceneEntity> SceneHierarchyReader::Children(const kb::ecs::World& world, SceneEntity entity) {
    if (!world.IsAlive(entity)) {
        return {};
    }

    std::vector<SceneEntity> children;
    ecs_iter_t it = ecs_children(world.NativeHandle(), entity.Id());
    while (ecs_children_next(&it)) {
        children.reserve(children.size() + static_cast<std::size_t>(std::max(0, it.count)));
        for (int32_t i = 0; i < it.count; ++i) {
            children.push_back(SceneEntity{ it.entities[i] });
        }
    }

    return children;
}

} // namespace kb::scene
