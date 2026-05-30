#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/TypeIds.hpp"

struct ecs_world_t;

namespace kb::ecs {

class TagStorage {
public:
    static void Add(ecs_world_t* world, Entity entity, TagId tag) noexcept;
    [[nodiscard]] static bool Has(const ecs_world_t* world, Entity entity, TagId tag) noexcept;
    static void Remove(ecs_world_t* world, Entity entity, TagId tag) noexcept;
};

} // namespace kb::ecs
