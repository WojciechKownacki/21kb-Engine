#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>

struct ecs_world_t;

namespace kb::ecs {

class World;
class WorldRegistrySet;

class WorldInternalAccess {
public:
    [[nodiscard]] static ecs_world_t* Native(World& world) noexcept;
    [[nodiscard]] static const ecs_world_t* Native(const World& world) noexcept;
    [[nodiscard]] static WorldRegistrySet* Registries(World& world) noexcept;
    [[nodiscard]] static const WorldRegistrySet* Registries(const World& world) noexcept;
    [[nodiscard]] static Entity ResolveAliveEntity(const World& world, Entity::IdType entityIdWithoutGeneration) noexcept;

    [[nodiscard]] static const void* TryGetComponent(const World& world, Entity entity, ComponentId componentId);
    static void SetComponent(World& world, Entity entity, ComponentId componentId, std::size_t size, const void* component);
};

} // namespace kb::ecs
