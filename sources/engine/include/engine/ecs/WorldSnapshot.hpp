#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/ComponentTypeInfo.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace kb::ecs {

struct ComponentSnapshot {
    ComponentId componentId = 0;
    std::string componentName;
    std::vector<std::byte> data;
};

struct EntitySnapshot {
    Entity::IdType id = 0;
    std::string name;
    std::vector<ComponentSnapshot> components;
};

struct WorldSnapshot {
    std::vector<ComponentTypeInfo> componentTypes;
    std::vector<EntitySnapshot> entities;
};

} // namespace kb::ecs
