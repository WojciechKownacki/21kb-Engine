#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>

struct ecs_world_t;

namespace kb::ecs {

class WorldComponentReader {
public:
    [[nodiscard]] static bool Has(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    [[nodiscard]] static const void* TryGet(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    [[nodiscard]] static void* TryGetMutable(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
};

} // namespace kb::ecs
