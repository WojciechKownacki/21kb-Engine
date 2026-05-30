#pragma once

#include <cstddef>

namespace kb::ecs {

class ComponentFieldBounds {
public:
    [[nodiscard]] static bool Contains(std::size_t componentSize, std::size_t fieldOffset, std::size_t fieldSize) noexcept;
};

} // namespace kb::ecs
