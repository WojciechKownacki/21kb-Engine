#include "ecs/relation/HierarchyRelationService.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "ecs/relation/HierarchyChildrenCollector.hpp"
#include "ecs/relation/RelationStorage.hpp"

#include <flecs.h>

#include <unordered_set>

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
    const Entity currentParent = Parent(world, child);
    if (currentParent.IsValid()) {
        RelationStorage::Remove(world, child, EcsChildOf, currentParent);
    }
}

Entity HierarchyRelationService::Parent(const ecs_world_t* world, Entity child) noexcept {
    return RelationStorage::Target(world, child, EcsChildOf);
}

std::vector<Entity> HierarchyRelationService::Children(ecs_world_t* world, Entity parent) {
    return HierarchyChildrenCollector::Collect(world, parent);
}

bool HierarchyRelationService::WouldCreateCycle(const ecs_world_t* world, Entity child, Entity parent) noexcept {
    std::unordered_set<Entity::IdType> visitedAncestors;
    const ecs_entity_t childId = FlecsEntityId(child);
    for (ecs_entity_t ancestor = FlecsEntityId(parent); ancestor != 0; ancestor = ecs_get_parent(world, ancestor)) {
        if (ancestor == childId) {
            return true;
        }
        if (!visitedAncestors.insert(ancestor).second) {
            return true;
        }
    }
    return false;
}

} // namespace kb::ecs
