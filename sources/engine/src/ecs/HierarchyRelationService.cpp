#include "ecs/relation/HierarchyRelationService.hpp"

#include "ecs/relation/RelationStorage.hpp"

#include <flecs.h>

namespace kb::ecs {

void HierarchyRelationService::SetParent(ecs_world_t* world, Entity child, Entity parent) noexcept {
    if (world == nullptr || !child.IsValid()) {
        return;
    }

    if (!parent.IsValid()) {
        ClearParent(world, child);
        return;
    }

    if (child == parent || WouldCreateCycle(world, child, parent)) {
        return;
    }

    ClearParent(world, child);
    RelationStorage::Add(world, child, EcsChildOf, parent);
}

void HierarchyRelationService::ClearParent(ecs_world_t* world, Entity child) noexcept {
    for (Entity parent = Parent(world, child); parent.IsValid(); parent = Parent(world, child)) {
        RelationStorage::Remove(world, child, EcsChildOf, parent);
    }
}

Entity HierarchyRelationService::Parent(const ecs_world_t* world, Entity child) noexcept {
    return RelationStorage::Target(world, child, EcsChildOf);
}

bool HierarchyRelationService::WouldCreateCycle(const ecs_world_t* world, Entity child, Entity parent) noexcept {
    for (Entity ancestor = parent; ancestor.IsValid(); ancestor = Parent(world, ancestor)) {
        if (ancestor == child) {
            return true;
        }
    }
    return false;
}

} // namespace kb::ecs
