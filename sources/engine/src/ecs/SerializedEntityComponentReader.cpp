#include "ecs/serialization/SerializedEntityComponentReader.hpp"

#include "ecs/serialization/SerializedComponentReflectionResolver.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "engine/ecs/World.hpp"

namespace kb::ecs {

bool SerializedEntityComponentReader::Read(const World& world, Entity entity, ComponentId componentId, SerializedComponent& output) {
    const ComponentReflection* reflection = SerializedComponentReflectionResolver::Find(world, componentId);
    if (reflection == nullptr) {
        return false;
    }

    const void* component = WorldInternalAccess::TryGetComponent(world, entity, componentId);
    if (component == nullptr) {
        return false;
    }

    return ComponentSerializer::Serialize(component, *reflection, output);
}

} // namespace kb::ecs
