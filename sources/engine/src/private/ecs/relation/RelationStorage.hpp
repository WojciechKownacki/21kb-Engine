#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/TypeIds.hpp"

struct ecs_world_t;

namespace kb::ecs {

class RelationStorage {
public:
    static void Add(ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept;
    [[nodiscard]] static bool Has(const ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept;
    static void Remove(ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept;
    [[nodiscard]] static Entity Target(const ecs_world_t* world, Entity entity, RelationId relation, int index = 0) noexcept;
};

} // namespace kb::ecs
