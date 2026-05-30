#include "engine/ecs/World.hpp"

#include "ecs/reflection/ComponentReflectionRegistry.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

namespace kb::ecs {

const ComponentReflection* World::RegisterComponentReflection(
    ComponentId componentId,
    std::string_view name,
    std::size_t size,
    std::initializer_list<ComponentFieldDesc> fields) {
    return registries_ == nullptr ? nullptr : registries_->ComponentReflections().Register(componentId, name, size, fields);
}

const ComponentReflection* World::Reflection(ComponentId componentId) const noexcept {
    return registries_ == nullptr ? nullptr : registries_->ComponentReflections().Find(componentId);
}

const ComponentReflection* World::Reflection(std::string_view componentName) const noexcept {
    return registries_ == nullptr ? nullptr : registries_->ComponentReflections().Find(componentName);
}

} // namespace kb::ecs
