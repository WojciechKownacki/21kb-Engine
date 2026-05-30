#pragma once

#include "engine/ecs/Entity.hpp"

#include <string>

struct ecs_world_t;

namespace kb::ecs {

class EntityNameReader {
public:
    [[nodiscard]] static std::string Read(ecs_world_t* world, Entity entity);
};

} // namespace kb::ecs
