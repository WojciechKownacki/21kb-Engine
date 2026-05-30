#include "scene/hierarchy/SceneHierarchyOperations.hpp"

#include <flecs.h>

#include <algorithm>

namespace kb::scene {

SceneEntity SceneHierarchyOperations::Parent(const kb::ecs::World& world, SceneEntity entity) noexcept {
    if (!world.IsAlive(entity)) {
        return {};
    }

    const ecs_entity_t parent = ecs_get_parent(world.NativeHandle(), entity.Id());
    return parent == 0 ? SceneEntity{} : SceneEntity{ parent };
}

std::vector<SceneEntity> SceneHierarchyOperations::Children(const kb::ecs::World& world, SceneEntity entity) {
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

std::vector<SceneEntity> SceneHierarchyOperations::Roots(const kb::ecs::World& world, std::uint64_t transformComponentId) {
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

bool SceneHierarchyOperations::SetParent(kb::ecs::World& world, SceneEntity child, SceneEntity parent) noexcept {
    if (!world.IsAlive(child)) {
        return false;
    }
    if (parent.IsValid() && !world.IsAlive(parent)) {
        return false;
    }
    if (child == parent) {
        return false;
    }
    if (parent.IsValid() && WouldCreateCycle(world, child, parent)) {
        return false;
    }

    const ecs_entity_t childId = child.Id();
    if (const ecs_entity_t currentParent = ecs_get_parent(world.NativeHandle(), childId); currentParent != 0) {
        ecs_remove_id(world.NativeHandle(), childId, ecs_pair(EcsChildOf, currentParent));
    }

    if (parent.IsValid()) {
        ecs_add_id(world.NativeHandle(), childId, ecs_pair(EcsChildOf, parent.Id()));
    }

    return true;
}

bool SceneHierarchyOperations::WouldCreateCycle(const kb::ecs::World& world, SceneEntity child, SceneEntity parent) noexcept {
    ecs_entity_t cursor = parent.Id();
    while (cursor != 0) {
        if (cursor == child.Id()) {
            return true;
        }
        cursor = ecs_get_parent(world.NativeHandle(), cursor);
    }
    return false;
}

} // namespace kb::scene
