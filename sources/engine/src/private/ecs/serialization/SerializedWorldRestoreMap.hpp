#pragma once

#include "engine/ecs/Entity.hpp"

#include <unordered_map>

namespace kb::ecs {

class SerializedWorldRestoreMap {
public:
    void Store(Entity::IdType sourceId, Entity restoredEntity);
    [[nodiscard]] Entity Find(Entity::IdType sourceId) const noexcept;

private:
    std::unordered_map<Entity::IdType, Entity> entities_;
};

} // namespace kb::ecs
