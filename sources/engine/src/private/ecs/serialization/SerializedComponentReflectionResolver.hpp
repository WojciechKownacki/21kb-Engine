#pragma once

#include "engine/ecs/ComponentReflection.hpp"
#include "engine/ecs/ComponentSerialization.hpp"

namespace kb::ecs {

class World;

class SerializedComponentReflectionResolver {
public:
    [[nodiscard]] static const ComponentReflection* Find(const World& world, ComponentId componentId) noexcept;
    [[nodiscard]] static const ComponentReflection* Find(const World& world, const SerializedComponent& component) noexcept;
};

} // namespace kb::ecs
