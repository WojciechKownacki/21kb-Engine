#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace kb::ecs {

struct EntityComponentInspection {
    ComponentId id = 0;
    std::string name;
    std::size_t size = 0;
    std::size_t alignment = 0;
};

struct EntityInspection {
    Entity entity;
    std::string name;
    Entity parent;
    std::vector<EntityComponentInspection> components;
};

} // namespace kb::ecs
