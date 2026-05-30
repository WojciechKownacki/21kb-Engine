#pragma once

#include "engine/ecs/ComponentReflection.hpp"

#include <initializer_list>
#include <span>
#include <unordered_map>

namespace kb::ecs {

class ComponentReflectionRegistry {
public:
    [[nodiscard]] const ComponentReflection* Register(
        ComponentId componentId,
        std::string_view componentName,
        std::size_t componentSize,
        std::initializer_list<ComponentFieldDesc> fields);

    [[nodiscard]] const ComponentReflection* Find(ComponentId componentId) const noexcept;
    [[nodiscard]] const ComponentReflection* Find(std::string_view componentName) const noexcept;
    void Clear() noexcept;

private:
    std::unordered_map<ComponentId, ComponentReflection> reflections_;
};

} // namespace kb::ecs
