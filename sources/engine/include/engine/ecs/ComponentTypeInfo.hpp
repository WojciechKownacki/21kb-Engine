#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <cstddef>
#include <string>

namespace kb::ecs {

struct ComponentTypeInfo {
    ComponentId id = 0;
    std::string name;
    std::size_t size = 0;
    std::size_t alignment = 0;
};

} // namespace kb::ecs
