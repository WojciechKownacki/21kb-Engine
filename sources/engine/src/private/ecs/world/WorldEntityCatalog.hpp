#pragma once

#include "engine/ecs/Entity.hpp"

#include <span>
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
    std::vector<Entity> entities_;
};

} // namespace kb::ecs
