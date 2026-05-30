#pragma once

#include "engine/ecs/ComponentReflection.hpp"

#include <span>
#include <string_view>

namespace kb::ecs {

class ComponentReflectionFactory {
public:
    [[nodiscard]] static ComponentReflection Create(
        ComponentId componentId,
        std::string_view componentName,
        std::size_t componentSize,
        std::span<const ComponentFieldDesc> fields);
};

} // namespace kb::ecs
