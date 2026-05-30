#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>

struct ecs_world_t;

namespace kb::ecs {

class WorldComponentMutator {
public:
    static void Set(ecs_world_t* world, Entity entity, ComponentId componentId, std::size_t size, const void* component);
    static void Remove(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    static void MarkModified(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
};

} // namespace kb::ecs
