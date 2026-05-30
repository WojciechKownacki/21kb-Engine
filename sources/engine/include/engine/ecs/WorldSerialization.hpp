#pragma once

#include "engine/ecs/ComponentSerialization.hpp"
#include "engine/ecs/Entity.hpp"

#include <string>
#include <vector>

namespace kb::ecs {

struct SerializedEntity {
    Entity::IdType sourceId = 0;
    std::string name;
    Entity::IdType parentSourceId = 0;
    std::vector<SerializedComponent> components;
};

struct SerializedWorld {
    std::vector<SerializedEntity> entities;
};

} // namespace kb::ecs
