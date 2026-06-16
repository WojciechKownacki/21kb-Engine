#include "ecs/relation/HierarchyChildrenCollector.hpp"

#include <flecs.h>

#include <cstdint>

namespace kb::ecs {

std::vector<Entity> HierarchyChildrenCollector::Collect(ecs_world_t* world, Entity parent) {
    std::vector<Entity> children;
    if (world == nullptr || !parent.IsValid()) {
        return children;
    }

    ecs_iter_t iterator = ecs_children(world, ecs_strip_generation(parent.Id()));
    while (ecs_children_next(&iterator)) {
        for (int32_t row = 0; row < iterator.count; ++row) {
            children.push_back(Entity{ static_cast<Entity::IdType>(iterator.entities[row]) });
        }
    }
    return children;
}

} // namespace kb::ecs
