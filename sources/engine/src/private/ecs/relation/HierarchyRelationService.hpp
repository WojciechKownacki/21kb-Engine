#pragma once

#include "engine/ecs/Entity.hpp"

#include <vector>

struct ecs_world_t;

namespace kb::ecs {

class HierarchyRelationService {
public:
    static void SetParent(ecs_world_t* world, Entity child, Entity parent) noexcept;
    static void ClearParent(ecs_world_t* world, Entity child) noexcept;
    [[nodiscard]] static Entity Parent(const ecs_world_t* world, Entity child) noexcept;
    [[nodiscard]] static std::vector<Entity> Children(ecs_world_t* world, Entity parent);

private:
    [[nodiscard]] static bool WouldCreateCycle(const ecs_world_t* world, Entity child, Entity parent) noexcept;
};

} // namespace kb::ecs
