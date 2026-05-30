#include "ecs/serialization/SerializedWorldEntityFactory.hpp"

#include "ecs/serialization/SerializedWorldRestoreMap.hpp"
#include "engine/ecs/World.hpp"

namespace kb::ecs {

void SerializedWorldEntityFactory::CreateEntities(World& world, const SerializedWorld& source, SerializedWorldRestoreMap& restoredEntities) {
    for (const SerializedEntity& serializedEntity : source.entities) {
        const Entity entity = world.CreateEntity(serializedEntity.name);
        restoredEntities.Store(serializedEntity.sourceId, entity);
    }
}

} // namespace kb::ecs
