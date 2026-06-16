#include "scene/hierarchy/SceneHierarchyParenting.hpp"

#include "ecs/FlecsEntityIds.hpp"

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

    const ecs_entity_t childId = kb::ecs::FlecsEntityId(child);
    const SceneEntity currentParent = world.Parent(child);
    if (currentParent == parent) {
        return false;
    }

    if (currentParent.IsValid()) {
        ecs_remove_id(world.NativeHandle(), childId, ecs_pair(EcsChildOf, ecs_strip_generation(currentParent.Id())));
    }

    if (parent.IsValid()) {
        ecs_add_id(world.NativeHandle(), childId, ecs_pair(EcsChildOf, ecs_strip_generation(parent.Id())));
    }

    return true;
}

bool SceneHierarchyParenting::WouldCreateCycle(const kb::ecs::World& world, SceneEntity child, SceneEntity parent) noexcept {
    for (SceneEntity cursor = parent; cursor.IsValid(); cursor = world.Parent(cursor)) {
        if (cursor == child) {
            return true;
        }
    }
    return false;
}

} // namespace kb::scene
