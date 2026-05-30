#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <cstddef>
#include <string_view>

struct ecs_world_t;

namespace kb::ecs {

class ComponentTypeFactory {
public:
    [[nodiscard]] static ComponentId Create(ecs_world_t* world, std::string_view name, std::size_t size, std::size_t alignment);
};

} // namespace kb::ecs
