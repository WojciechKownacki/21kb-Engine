#include "ecs/serialization/SerializedComponentReflectionResolver.hpp"

#include "engine/ecs/World.hpp"

namespace kb::ecs {

const ComponentReflection* SerializedComponentReflectionResolver::Find(const World& world, ComponentId componentId) noexcept {
    return world.Reflection(componentId);
}

const ComponentReflection* SerializedComponentReflectionResolver::Find(const World& world, const SerializedComponent& component) noexcept {
    const ComponentReflection* reflection = world.Reflection(component.componentName);
    if (reflection == nullptr) {
        reflection = world.Reflection(component.componentId);
    }
    return reflection;
}

} // namespace kb::ecs
