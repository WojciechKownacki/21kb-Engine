#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::ecs {

struct EntityComponentInspection {
    ComponentId id = 0;
    std::string name;
    std::size_t size = 0;
    std::size_t alignment = 0;
};

struct EntityInspection {
    Entity entity;
    std::string name;
    Entity parent;
    std::vector<EntityComponentInspection> components;
};

struct WorldArchetypeChunkInspection {
    std::size_t chunkIndex = 0;
    std::size_t liveEntities = 0;
    std::size_t capacity = 0;
    std::size_t payloadBytes = 0;
    std::size_t usedBytes = 0;
    std::size_t wastedBytes = 0;
    double occupancyPercent = 0.0;
    double wastedPercent = 0.0;
};

struct WorldArchetypeInspection {
    std::size_t archetypeIndex = 0;
    std::vector<EntityComponentInspection> components;
    std::size_t liveEntities = 0;
    std::size_t chunks = 0;
    std::size_t capacity = 0;
    std::size_t payloadBytes = 0;
    std::size_t usedBytes = 0;
    std::size_t wastedBytes = 0;
    std::uint64_t version = 0;
    double occupancyPercent = 0.0;
    double wastedPercent = 0.0;
    std::vector<WorldArchetypeChunkInspection> chunkInspections;
};

} // namespace kb::ecs
