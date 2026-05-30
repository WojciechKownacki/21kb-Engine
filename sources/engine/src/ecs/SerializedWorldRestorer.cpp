#include "ecs/serialization/SerializedWorldRestorer.hpp"

#include "ecs/serialization/SerializedWorldComponentRestorer.hpp"
#include "ecs/serialization/SerializedWorldEntityFactory.hpp"
#include "ecs/serialization/SerializedWorldParentRestorer.hpp"
#include "ecs/serialization/SerializedWorldRestoreMap.hpp"

namespace kb::ecs {

SerializedWorldRestorer::SerializedWorldRestorer(World& world) noexcept
    : world_(world) {}

bool SerializedWorldRestorer::Restore(const SerializedWorld& source) const {
    SerializedWorldRestoreMap restoredEntities;
    SerializedWorldEntityFactory::CreateEntities(world_, source, restoredEntities);
    return SerializedWorldComponentRestorer::RestoreComponents(world_, source, restoredEntities)
        && SerializedWorldParentRestorer::RestoreParents(world_, source, restoredEntities);
}

} // namespace kb::ecs
