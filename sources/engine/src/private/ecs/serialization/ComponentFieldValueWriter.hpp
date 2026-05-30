#pragma once

#include "engine/ecs/ComponentSerialization.hpp"

namespace kb::ecs {

class ComponentFieldValueWriter {
public:
    [[nodiscard]] static bool Write(void* component, std::size_t componentSize, const ComponentFieldReflection& field, const ComponentFieldValue& value) noexcept;
};

} // namespace kb::ecs
