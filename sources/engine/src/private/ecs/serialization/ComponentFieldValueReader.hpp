#pragma once

#include "engine/ecs/ComponentSerialization.hpp"

namespace kb::ecs {

class ComponentFieldValueReader {
public:
    [[nodiscard]] static bool Read(const void* component, std::size_t componentSize, const ComponentFieldReflection& field, ComponentFieldValue& output) noexcept;
};

} // namespace kb::ecs
