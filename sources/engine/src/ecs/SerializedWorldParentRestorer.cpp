#include "ecs/serialization/SerializedWorldParentRestorer.hpp"

#include "ecs/serialization/SerializedWorldRestoreMap.hpp"
#include "engine/ecs/World.hpp"

namespace kb::ecs {

bool SerializedWorldParentRestorer::RestoreParents(World& world, const SerializedWorld& source, const SerializedWorldRestoreMap& restoredEntities) {
    for (const SerializedEntity& serializedEntity : source.entities) {
        if (serializedEntity.parentSourceId == 0) {
            continue;
        }

        const Entity child = restoredEntities.Find(serializedEntity.sourceId);
        const Entity parent = restoredEntities.Find(serializedEntity.parentSourceId);
        if (!child.IsValid() || !parent.IsValid()) {
            return false;
        }
        world.SetParent(child, parent);
    }

    return true;
}

} // namespace kb::ecs
