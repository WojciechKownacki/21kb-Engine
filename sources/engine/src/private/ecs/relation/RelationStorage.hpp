#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/TypeIds.hpp"

#include <cstddef>
#include <span>

struct ecs_world_t;

namespace kb::ecs {

class RelationStorage {
public:
    static void Add(ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept;
    [[nodiscard]] static std::size_t AddKnownAlivePairs(
        ecs_world_t* world,
        std::span<const Entity> entities,
        RelationId relation,
        std::span<const Entity> targets) noexcept;
    [[nodiscard]] static bool Has(const ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept;
    static void Remove(ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept;
    [[nodiscard]] static Entity Target(const ecs_world_t* world, Entity entity, RelationId relation, int index = 0) noexcept;
};

} // namespace kb::ecs
