#pragma once

#include "engine/ecs/ComponentReflection.hpp"

#include <span>

namespace kb::ecs {

class ComponentReflectionValidator {
public:
    [[nodiscard]] static bool Validate(ComponentId componentId, std::size_t componentSize, std::span<const ComponentFieldDesc> fields) noexcept;

private:
    [[nodiscard]] static bool IsFieldInBounds(std::size_t componentSize, const ComponentFieldDesc& field) noexcept;
};

} // namespace kb::ecs
