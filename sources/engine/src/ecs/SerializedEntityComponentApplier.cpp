#include "ecs/serialization/SerializedEntityComponentApplier.hpp"

#include "ecs/serialization/SerializedComponentReflectionResolver.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "engine/ecs/World.hpp"

#include <cstring>
#include <vector>

namespace kb::ecs {

void SerializedEntityComponentApplier::CopyExistingComponent(
    const World& world,
    Entity entity,
    const ComponentReflection& reflection,
    std::vector<std::byte>& componentBuffer) {
    const void* existingComponent = WorldInternalAccess::TryGetComponent(world, entity, reflection.Id());
    if (existingComponent != nullptr) {
        std::memcpy(componentBuffer.data(), existingComponent, componentBuffer.size());
    }
}

bool SerializedEntityComponentApplier::Apply(World& world, Entity entity, const SerializedComponent& component) {
    const ComponentReflection* reflection = SerializedComponentReflectionResolver::Find(world, component);
    if (reflection == nullptr) {
        return false;
    }

    std::vector<std::byte> componentBuffer(reflection->Size(), std::byte{});
    CopyExistingComponent(world, entity, *reflection, componentBuffer);
    if (!ComponentSerializer::Apply(component, *reflection, componentBuffer.data())) {
        return false;
    }

    WorldInternalAccess::SetComponent(world, entity, reflection->Id(), reflection->Size(), componentBuffer.data());
    return true;
}

} // namespace kb::ecs
