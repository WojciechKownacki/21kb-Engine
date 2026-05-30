#include "ecs/world/WorldInternalAccess.hpp"

#include "engine/ecs/World.hpp"

namespace kb::ecs {

ecs_world_t* WorldInternalAccess::Native(World& world) noexcept {
    return world.world_;
}

const ecs_world_t* WorldInternalAccess::Native(const World& world) noexcept {
    return world.world_;
}

WorldRegistrySet* WorldInternalAccess::Registries(World& world) noexcept {
    return world.registries_.get();
}

const WorldRegistrySet* WorldInternalAccess::Registries(const World& world) noexcept {
    return world.registries_.get();
}

const void* WorldInternalAccess::TryGetComponent(const World& world, Entity entity, ComponentId componentId) noexcept {
    return world.TryGetComponent(entity, componentId);
}

void WorldInternalAccess::SetComponent(World& world, Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    world.SetComponent(entity, componentId, size, component);
}

} // namespace kb::ecs
