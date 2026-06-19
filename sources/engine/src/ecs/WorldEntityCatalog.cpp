#include "ecs/world/WorldEntityCatalog.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

#include <algorithm>
#include <vector>

namespace kb::ecs {

void WorldEntityCatalog::Add(Entity entity) {
    if (entity.IsValid()) {
        entities_.push_back(entity);
    }
}

void WorldEntityCatalog::AddMany(std::span<const Entity> entities) {
    entities_.reserve(entities_.size() + entities.size());
    for (Entity entity : entities) {
        if (entity.IsValid()) {
            entities_.push_back(entity);
        }
    }
}

void WorldEntityCatalog::Remove(Entity entity) noexcept {
    entities_.erase(std::remove(entities_.begin(), entities_.end(), entity), entities_.end());
}

void WorldEntityCatalog::RemoveMany(std::span<const Entity> entities) {
    if (entities.empty() || entities_.empty()) {
        return;
    }

    std::vector<Entity::IdType> removedIds;
    removedIds.reserve(entities.size());
    for (Entity entity : entities) {
        if (entity.IsValid()) {
            removedIds.push_back(entity.Id());
        }
    }
    if (removedIds.empty()) {
        return;
    }
    std::sort(removedIds.begin(), removedIds.end());
    removedIds.erase(std::unique(removedIds.begin(), removedIds.end()), removedIds.end());

    entities_.erase(
        std::remove_if(
            entities_.begin(),
            entities_.end(),
            [&removedIds](Entity entity) {
                return std::binary_search(removedIds.begin(), removedIds.end(), entity.Id());
            }),
        entities_.end());
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
        if (entity.IsValid() && ecs_is_valid(world, FlecsEntityId(entity)) && ecs_is_alive(world, FlecsEntityId(entity))) {
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
