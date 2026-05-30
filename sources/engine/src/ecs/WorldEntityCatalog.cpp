#include "ecs/world/WorldEntityCatalog.hpp"

#include <flecs.h>

#include <algorithm>

namespace kb::ecs {

void WorldEntityCatalog::Add(Entity entity) {
    if (entity.IsValid()) {
        entities_.push_back(entity);
    }
}

void WorldEntityCatalog::Remove(Entity entity) noexcept {
    entities_.erase(std::remove(entities_.begin(), entities_.end(), entity), entities_.end());
}

void WorldEntityCatalog::Clear() noexcept {
    entities_.clear();
}

std::vector<Entity> WorldEntityCatalog::AliveEntities(const ecs_world_t* world) const {
    std::vector<Entity> alive;
    if (world == nullptr) {
        return alive;
    }

    alive.reserve(entities_.size());
    for (Entity entity : entities_) {
        if (entity.IsValid() && ecs_is_valid(world, entity.Id()) && ecs_is_alive(world, entity.Id())) {
            alive.push_back(entity);
        }
    }

    std::sort(alive.begin(), alive.end(), [](Entity lhs, Entity rhs) {
        return lhs.Id() < rhs.Id();
    });
    alive.erase(std::unique(alive.begin(), alive.end()), alive.end());
    return alive;
}

} // namespace kb::ecs
