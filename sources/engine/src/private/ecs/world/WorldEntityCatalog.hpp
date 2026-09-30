#pragma once

#include "engine/ecs/Entity.hpp"

#include <cstddef>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

struct ecs_world_t;

namespace kb::ecs {

class WorldEntityCatalog {
public:
    void Add(Entity entity);
    void AddMany(std::span<const Entity> entities);
    void Remove(Entity entity) noexcept;
    void RemoveMany(std::span<const Entity> entities);
    void Clear() noexcept;

    [[nodiscard]] std::vector<Entity> AliveEntities(const ecs_world_t* world) const;

private:
    static constexpr std::size_t kInvalidPosition = std::numeric_limits<std::size_t>::max();

    std::vector<Entity> entities_;
    std::vector<std::size_t> densePositions_;
    std::unordered_map<Entity::IdType, std::size_t> positions_;
};

} // namespace kb::ecs
