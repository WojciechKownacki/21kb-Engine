#include "ecs/serialization/SerializedWorldComponentRestorer.hpp"

#include "ecs/serialization/SerializedComponentReflectionResolver.hpp"
#include "ecs/serialization/SerializedWorldRestoreMap.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "engine/ecs/ComponentSerialization.hpp"
#include "engine/ecs/World.hpp"

#include <cstddef>
#include <vector>

namespace kb::ecs {

namespace {

[[nodiscard]] bool RestoreComponent(World& world, Entity entity, const SerializedComponent& serializedComponent, std::vector<std::byte>& componentBuffer) {
    const ComponentReflection* reflection = SerializedComponentReflectionResolver::Find(world, serializedComponent);
    if (reflection == nullptr) {
        return false;
    }

    componentBuffer.assign(reflection->Size(), std::byte{});
    if (!ComponentSerializer::Apply(serializedComponent, *reflection, componentBuffer.data())) {
        return false;
    }

    WorldInternalAccess::SetComponent(world, entity, reflection->Id(), reflection->Size(), componentBuffer.data());
    return true;
}

} // namespace

bool SerializedWorldComponentRestorer::RestoreComponents(World& world, const SerializedWorld& source, const SerializedWorldRestoreMap& restoredEntities) {
    std::vector<std::byte> componentBuffer;
    for (const SerializedEntity& serializedEntity : source.entities) {
        const Entity entity = restoredEntities.Find(serializedEntity.sourceId);
        if (!entity.IsValid()) {
            return false;
        }

        for (const SerializedComponent& serializedComponent : serializedEntity.components) {
            if (!RestoreComponent(world, entity, serializedComponent, componentBuffer)) {
                return false;
            }
        }
    }

    return true;
}

} // namespace kb::ecs
