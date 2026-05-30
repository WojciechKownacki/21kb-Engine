#include "ecs/world/WorldComponentRegistrar.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

namespace kb::ecs {

ComponentId WorldComponentRegistrar::Register(
    ecs_world_t* world,
    WorldRegistrySet* registries,
    std::type_index type,
    std::string_view name,
    std::size_t size,
    std::size_t alignment) {
    return registries == nullptr ? 0 : registries->Components().Register(world, type, name, size, alignment);
}

ComponentId WorldComponentRegistrar::Find(const WorldRegistrySet* registries, std::type_index type) noexcept {
    return registries == nullptr ? 0 : registries->Components().Find(type);
}

} // namespace kb::ecs
