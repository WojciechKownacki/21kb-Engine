#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/ComponentStoragePolicy.hpp"

#include <cstddef>
#include <string_view>
#include <typeindex>

struct ecs_world_t;

namespace kb::ecs {

class WorldRegistrySet;

class WorldComponentRegistrar {
public:
    [[nodiscard]] static ComponentId Register(
        ecs_world_t* world,
        WorldRegistrySet* registries,
        std::type_index type,
        std::string_view name,
        std::size_t size,
        std::size_t alignment,
        ComponentRegistrationOptions options);

    [[nodiscard]] static ComponentId Find(const WorldRegistrySet* registries, std::type_index type) noexcept;
};

} // namespace kb::ecs
