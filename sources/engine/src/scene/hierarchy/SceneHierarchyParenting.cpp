#include "scene/hierarchy/SceneHierarchyParenting.hpp"

#include <flecs.h>

namespace kb::scene {

bool SceneHierarchyParenting::SetParent(kb::ecs::World& world, SceneEntity child, SceneEntity parent) noexcept {
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

bool SceneHierarchyParenting::WouldCreateCycle(const kb::ecs::World& world, SceneEntity child, SceneEntity parent) noexcept {
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
