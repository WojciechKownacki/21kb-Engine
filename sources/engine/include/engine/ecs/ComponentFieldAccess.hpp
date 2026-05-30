#pragma once

#include "engine/ecs/ComponentReflection.hpp"

#include <cstddef>
#include <span>

namespace kb::ecs {

class ComponentFieldAccess {
public:
    [[nodiscard]] static bool Read(const void* component, std::size_t componentSize, const ComponentFieldReflection& field, std::span<std::byte> output) noexcept;
    [[nodiscard]] static bool Write(void* component, std::size_t componentSize, const ComponentFieldReflection& field, std::span<const std::byte> input) noexcept;

private:
    [[nodiscard]] static bool CanAccess(const void* component, std::size_t componentSize, const ComponentFieldReflection& field, std::size_t bufferSize) noexcept;
};

} // namespace kb::ecs
