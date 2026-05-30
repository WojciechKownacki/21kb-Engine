#pragma once

#include "engine/ecs/WorldSerialization.hpp"

#include <cstddef>
#include <string_view>
#include <unordered_map>

namespace kb::ecs {

class SerializedWorldEntityIndex {
public:
    explicit SerializedWorldEntityIndex(SerializedWorld& world) noexcept;

    [[nodiscard]] SerializedEntity& Ensure(Entity::IdType sourceId, std::string_view name);
    [[nodiscard]] SerializedEntity* Find(Entity::IdType sourceId) noexcept;
    [[nodiscard]] const SerializedEntity* Find(Entity::IdType sourceId) const noexcept;

private:
    SerializedWorld& world_;
    std::unordered_map<Entity::IdType, std::size_t> indices_;
};

} // namespace kb::ecs
