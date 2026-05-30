#pragma once

#include "engine/ecs/Entity.hpp"

#include <vector>

struct ecs_world_t;

namespace kb::ecs {

class HierarchyChildrenCollector {
public:
    [[nodiscard]] static std::vector<Entity> Collect(ecs_world_t* world, Entity parent);
};

} // namespace kb::ecs
