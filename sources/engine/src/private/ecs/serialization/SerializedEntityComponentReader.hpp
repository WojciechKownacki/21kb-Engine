#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/ComponentSerialization.hpp"
#include "engine/ecs/Entity.hpp"

namespace kb::ecs {

class World;

class SerializedEntityComponentReader {
public:
    [[nodiscard]] static bool Read(const World& world, Entity entity, ComponentId componentId, SerializedComponent& output);
};

} // namespace kb::ecs
