#include "ecs/serialization/SerializedWorldRestoreMap.hpp"

namespace kb::ecs {

void SerializedWorldRestoreMap::Store(Entity::IdType sourceId, Entity restoredEntity) {
    if (sourceId != 0 && restoredEntity.IsValid()) {
        entities_.emplace(sourceId, restoredEntity);
    }
}

Entity SerializedWorldRestoreMap::Find(Entity::IdType sourceId) const noexcept {
    const auto it = entities_.find(sourceId);
    return it == entities_.end() ? Entity{} : it->second;
}

} // namespace kb::ecs
