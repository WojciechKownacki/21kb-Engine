#pragma once

#include "engine/ecs/Entity.hpp"
#include "ecs/type/OwnedTypeName.hpp"

#include <string_view>
#include <typeindex>
#include <unordered_map>

struct ecs_world_t;

namespace kb::ecs {

class TypeEntityCache {
public:
    [[nodiscard]] Entity::IdType Register(ecs_world_t* world, std::type_index type, std::string_view name);
    [[nodiscard]] Entity::IdType Find(std::type_index type) const noexcept;
    void Clear() noexcept;

private:
    [[nodiscard]] static Entity::IdType CreateTypeEntity(ecs_world_t* world, std::string_view name);

    std::unordered_map<std::string, Entity::IdType, OwnedTypeNameHash, std::equal_to<>> ids_;
};

} // namespace kb::ecs
