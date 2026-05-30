#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

struct ecs_world_t;

namespace kb::ecs {

class ComponentStorageQuery {
public:
    [[nodiscard]] static bool Has(const ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    [[nodiscard]] static const void* TryGet(const ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
    [[nodiscard]] static void* TryGetMutable(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept;
};

} // namespace kb::ecs
