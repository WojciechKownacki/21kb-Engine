#include "ecs/world/WorldEntityCatalog.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

#include <algorithm>
#include <vector>

namespace kb::ecs {

void WorldEntityCatalog::Add(Entity entity) {
    if (!entity.IsValid()) {
        return;
    }
    const std::uint32_t denseIndex = GeneratedEntityIndex(entity);
    if (denseIndex < densePositions_.size()) {
        const std::size_t position = densePositions_[denseIndex];
        if (position != kInvalidPosition && entities_[position] == entity) {
            return;
        }
    }
    if (positions_.contains(entity.Id())) {
        return;
    }

    entities_.push_back(entity);
    try {
        if (denseIndex == densePositions_.size()) {
            densePositions_.push_back(kInvalidPosition);
        }
        if (denseIndex < densePositions_.size() && densePositions_[denseIndex] == kInvalidPosition) {
            densePositions_[denseIndex] = entities_.size() - 1U;
        } else {
            positions_.emplace(entity.Id(), entities_.size() - 1U);
        }
    } catch (...) {
        entities_.pop_back();
        throw;
    }
}

void WorldEntityCatalog::AddMany(std::span<const Entity> entities) {
    entities_.reserve(entities_.size() + entities.size());
    std::size_t anticipatedDenseSize = densePositions_.size();
    for (Entity entity : entities) {
        if (GeneratedEntityIndex(entity) == anticipatedDenseSize) {
            ++anticipatedDenseSize;
        }
    }
    densePositions_.reserve(anticipatedDenseSize);
    for (Entity entity : entities) {
        Add(entity);
    }
}

void WorldEntityCatalog::Remove(Entity entity) noexcept {
    const std::uint32_t denseIndex = GeneratedEntityIndex(entity);
    bool dense = false;
    std::size_t position = kInvalidPosition;
    if (denseIndex < densePositions_.size()) {
        position = densePositions_[denseIndex];
        dense = position != kInvalidPosition && entities_[position] == entity;
    }
    auto found = positions_.end();
    if (!dense) {
        found = positions_.find(entity.Id());
        if (found == positions_.end()) {
            return;
        }
        position = found->second;
    }

    const std::size_t lastPosition = entities_.size() - 1U;
    const Entity last = entities_.back();
    if (position != lastPosition) {
        const std::uint32_t lastDenseIndex = GeneratedEntityIndex(last);
        if (lastDenseIndex < densePositions_.size() && densePositions_[lastDenseIndex] == lastPosition) {
            densePositions_[lastDenseIndex] = position;
        } else {
            positions_.find(last.Id())->second = position;
        }
        entities_[position] = last;
    }
    entities_.pop_back();
    if (dense) {
        densePositions_[denseIndex] = kInvalidPosition;
    } else {
        positions_.erase(found);
    }
}

void WorldEntityCatalog::RemoveMany(std::span<const Entity> entities) {
    for (Entity entity : entities) {
        Remove(entity);
    }
}

void WorldEntityCatalog::Clear() noexcept {
    entities_.clear();
    densePositions_.clear();
    positions_.clear();
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
